#include "doctest_compatibility.h"

#include "gucc/file_utils.hpp"
#include "gucc/logger.hpp"
#include "gucc/luks.hpp"
#include "gucc/testing/command_stub.hpp"

#include <sys/stat.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

using gucc::utils::testing::ScopedCommandStub;

using namespace std::string_view_literals;

namespace {

void install_noop_logger() {
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) { });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);
}

constexpr auto kLuks2Dump = "LUKS header information\n"
                            "Version:       \t2\n"
                            "Epoch:         \t5\n"
                            "\n"
                            "Data segments:\n"
                            "  0: crypt\n"
                            "\toffset: 16777216 [bytes]\n"
                            "\n"
                            "Keyslots:\n"
                            "  0: luks2\n"
                            "\tKey:        512 bits\n"
                            "\tPriority:   normal\n"
                            "\tPBKDF:      argon2id\n"
                            "\tArea offset:32768 [bytes]\n"
                            "  2: luks2\n"
                            "\tKey:        512 bits\n"
                            "\tPBKDF:      pbkdf2\n"
                            "Tokens:\n"
                            "Digests:\n"
                            "  0: pbkdf2\n"
                            "\tHash:       sha256\n"sv;

constexpr auto kLuks2ProdDump = R"(LUKS header information
Version:        2
Epoch:          4
Metadata area:  16384 [bytes]
Keyslots area:  16744448 [bytes]
UUID:           00000000-0000-0000-0000-000000000000
Label:          (no label)
Subsystem:      (no subsystem)
Flags:          allow-discards

Data segments:
  0: crypt
        offset: 16777216 [bytes]
        length: (whole device)
        cipher: aes-xts-plain64
        sector: 512 [bytes]

Keyslots:
  0: luks2
        Key:        512 bits
        Priority:   normal
        Cipher:     aes-xts-plain64
        Cipher key: 512 bits
        PBKDF:      argon2id
        Time cost:  18
        Memory:     1048576
        Threads:    4
        Salt:       00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
                    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        AF stripes: 4000
        AF hash:    sha512
        Area offset:32768 [bytes]
        Area length:258048 [bytes]
        Digest ID:  0
Tokens:
Digests:
  0: pbkdf2
        Hash:       sha512
        Iterations: 533355
        Salt:       00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
                    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        Digest:     00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
                    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
)"sv;

constexpr auto kLuks1Dump = R"(LUKS header information for /dev/sda2

Version:        1
Cipher name:    aes
Key Slot 0: ENABLED
    Iterations:             2000000
Key Slot 1: ENABLED
Key Slot 2: DISABLED
Key Slot 3: DISABLED
Key Slot 4: DISABLED
Key Slot 5: DISABLED
Key Slot 6: DISABLED
Key Slot 7: DISABLED
)"sv;

auto file_mode(const fs::path& path) -> mode_t {
    struct stat st{};
    REQUIRE_EQ(::stat(path.c_str(), &st), 0);
    return st.st_mode & 0777;
}

}  // namespace

TEST_CASE("keyslots")
{
    namespace detail = gucc::crypto::detail;

    SECTION("luks1")
    {
        CHECK_EQ(detail::count_enabled_luks_keyslots(kLuks1Dump), 2);
    }
    SECTION("luks2")
    {
        CHECK_EQ(detail::count_enabled_luks_keyslots(kLuks2Dump), 2);
        CHECK_EQ(detail::count_enabled_luks_keyslots(kLuks2ProdDump), 1);
    }
    SECTION("empty")
    {
        CHECK_EQ(detail::count_enabled_luks_keyslots(""sv), 0);
        CHECK_EQ(detail::count_enabled_luks_keyslots("Key Slot 0: DISABLED\n"sv), 0);
    }
}

TEST_CASE("luks_keyslot_count")
{
    install_noop_logger();

    SECTION("simple dump")
    {
        ScopedCommandStub stub;
        stub.on_exact("/usr/bin/cryptsetup luksDump /dev/sda2", {.output = std::string{kLuks1Dump}});
        const auto count = gucc::crypto::luks_keyslot_count("/dev/sda2"sv);
        REQUIRE(count);
        CHECK_EQ(*count, 2);
    }
    SECTION("failed dump")
    {
        ScopedCommandStub stub;
        stub.on_exact("/usr/bin/cryptsetup luksDump /dev/sda2", {.output = "Device /dev/sda2 is not a valid LUKS device.", .exit_code = 1});
        const auto count = gucc::crypto::luks_keyslot_count("/dev/sda2"sv);
        REQUIRE_FALSE(count);
        CHECK_EQ(count.error().code, gucc::ErrorCode::SubprocessFailed);
    }
}

TEST_CASE("luks_setup_keyfile")
{
    install_noop_logger();

    const auto root = fs::temp_directory_path() / "gucc-luks-keyfile";
    fs::remove_all(root);
    fs::create_directories(root / "etc");
    REQUIRE(gucc::file_utils::write_file((root / "etc/mkinitcpio.conf").string(), "MODULES=()\nBINARIES=()\nFILES=()\nHOOKS=(base udev)\n"sv));

    const auto keyfile = root / "crypto_keyfile.bin";

    const auto dd_cmd = "dd bs=512 count=4 if=/dev/urandom of=" + keyfile.string() + " iflag=fullblock";

    ScopedCommandStub stub;
    stub.on([&](const gucc::utils::InterceptedCommand& cmd) {
        if (cmd.command_line == dd_cmd) {
            static_cast<void>(gucc::file_utils::write_file(keyfile.string(), "key"sv));
        }
        return true;
    },
        {});

    SECTION("fresh keyfile")
    {
        REQUIRE(gucc::crypto::luks_setup_keyfile(keyfile.string(), root.string(), "/dev/sda2"sv));
        CHECK_EQ(file_mode(keyfile), 0600);
        CHECK_EQ(stub.command_lines(), std::vector<std::string>{dd_cmd, "cryptsetup -q  luksAddKey /dev/sda2 " + keyfile.string(), "mkinitcpio -P"});
    }
    SECTION("existing keyfile is kept")
    {
        REQUIRE(gucc::file_utils::write_file(keyfile.string(), "old"sv));
        fs::permissions(keyfile, fs::perms::all);
        REQUIRE(gucc::crypto::luks_setup_keyfile(keyfile.string(), root.string(), "/dev/sda2"sv));
        CHECK_EQ(fs::file_size(keyfile), 3);
        CHECK_EQ(file_mode(keyfile), 0600);
        CHECK_EQ(stub.count_prefix("dd "sv), 0U);
    }

    fs::remove_all(root);
}
