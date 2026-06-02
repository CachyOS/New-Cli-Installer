#include "doctest_compatibility.h"

#include "gucc/file_utils.hpp"
#include "gucc/logger.hpp"
#include "gucc/luks_swap.hpp"

#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

#include <fmt/format.h>
#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace {

class TempRoot final {
 public:
    TempRoot()
      : m_path(fs::temp_directory_path() / fs::path{fmt::format("gucc-luks-swap-{}", std::random_device{}())}) {
        fs::create_directories(m_path);
    }
    ~TempRoot() {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }
    TempRoot(const TempRoot&)                    = delete;
    auto operator=(const TempRoot&) -> TempRoot& = delete;
    TempRoot(TempRoot&&)                         = delete;
    auto operator=(TempRoot&&) -> TempRoot&      = delete;

    [[nodiscard]] auto path() const noexcept -> const fs::path& { return m_path; }

 private:
    fs::path m_path;
};

void install_silent_logger() {
    auto sink   = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {});
    auto logger = std::make_shared<spdlog::logger>("default", sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);
}

}  // namespace

TEST_CASE("luks_swap::make_crypttab_line")
{
    SECTION("defaults match wiki-canonical aes-xts-plain64 + sector-size=4096")
    {
        const gucc::luks_swap::RandomKeyConfig cfg{.source_device = "/dev/sda3"};
        REQUIRE_EQ(gucc::luks_swap::make_crypttab_line(cfg),
            "swap /dev/sda3 /dev/urandom swap,cipher=aes-xts-plain64,size=512,sector-size=4096\n");
    }
    SECTION("custom mapper name, cipher, key_size")
    {
        const gucc::luks_swap::RandomKeyConfig cfg{
            .mapper_name   = "cryptswap",
            .source_device = "PARTUUID=abcd-1234",
            .cipher        = "aes-cbc-essiv:sha256",
            .key_size      = 256,
        };
        REQUIRE_EQ(gucc::luks_swap::make_crypttab_line(cfg),
            "cryptswap PARTUUID=abcd-1234 /dev/urandom swap,cipher=aes-cbc-essiv:sha256,size=256,sector-size=4096\n");
    }
    SECTION("sector_size=0 omits the sector-size clause entirely")
    {
        const gucc::luks_swap::RandomKeyConfig cfg{
            .source_device = "/dev/sda3",
            .sector_size   = 0,
        };
        REQUIRE_EQ(gucc::luks_swap::make_crypttab_line(cfg),
            "swap /dev/sda3 /dev/urandom swap,cipher=aes-xts-plain64,size=512\n");
    }
}

TEST_CASE("luks_swap::make_fstab_line")
{
    REQUIRE_EQ(gucc::luks_swap::make_fstab_line("swap"sv),
        "/dev/mapper/swap none swap defaults 0 0\n");
    REQUIRE_EQ(gucc::luks_swap::make_fstab_line("cryptswap"sv),
        "/dev/mapper/cryptswap none swap defaults 0 0\n");
}

TEST_CASE("luks_swap::add_crypttab_entry")
{
    install_silent_logger();
    const gucc::luks_swap::RandomKeyConfig cfg{.source_device = "/dev/sda3"};

    SECTION("creates /etc/crypttab when missing")
    {
        TempRoot root;
        REQUIRE(gucc::luks_swap::add_crypttab_entry(cfg, root.path().string()));
        const auto body = gucc::file_utils::read_whole_file((root.path() / "etc" / "crypttab").string());
        REQUIRE(body.contains("swap /dev/sda3 /dev/urandom"sv));
    }
    SECTION("preserves unrelated entries and comments")
    {
        TempRoot root;
        fs::create_directories(root.path() / "etc");
        constexpr auto kPre = "# header comment\nroot /dev/sda2 none luks\n"sv;
        gucc::file_utils::create_file_for_overwrite(
            (root.path() / "etc" / "crypttab").string(), kPre);

        REQUIRE(gucc::luks_swap::add_crypttab_entry(cfg, root.path().string()));

        const auto body = gucc::file_utils::read_whole_file((root.path() / "etc" / "crypttab").string());
        REQUIRE(body.contains("# header comment\n"sv));
        REQUIRE(body.contains("root /dev/sda2 none luks\n"sv));
        REQUIRE(body.contains("swap /dev/sda3"sv));
    }
    SECTION("replaces an existing swap entry instead of duplicating")
    {
        TempRoot root;
        fs::create_directories(root.path() / "etc");
        gucc::file_utils::create_file_for_overwrite(
            (root.path() / "etc" / "crypttab").string(),
            "swap /dev/old /dev/urandom swap,cipher=aes-cbc,size=256\n"sv);

        REQUIRE(gucc::luks_swap::add_crypttab_entry(cfg, root.path().string()));

        const auto body = gucc::file_utils::read_whole_file((root.path() / "etc" / "crypttab").string());
        REQUIRE_FALSE(body.contains("/dev/old"sv));
        REQUIRE(body.contains("swap /dev/sda3"sv));
    }
    SECTION("exist")
    {
        TempRoot root;
        REQUIRE(gucc::luks_swap::add_crypttab_entry(cfg, root.path().string()));
        REQUIRE(gucc::luks_swap::add_crypttab_entry(cfg, root.path().string()));
        const auto body = gucc::file_utils::read_whole_file((root.path() / "etc" / "crypttab").string());
        REQUIRE_EQ(body, "swap /dev/sda3 /dev/urandom swap,cipher=aes-xts-plain64,size=512,sector-size=4096\n");
    }
    SECTION("does not match a commented-out line with the same name")
    {
        TempRoot root;
        fs::create_directories(root.path() / "etc");
        gucc::file_utils::create_file_for_overwrite(
            (root.path() / "etc" / "crypttab").string(),
            "# swap /dev/old /dev/urandom swap\n"sv);

        REQUIRE(gucc::luks_swap::add_crypttab_entry(cfg, root.path().string()));

        const auto body = gucc::file_utils::read_whole_file((root.path() / "etc" / "crypttab").string());
        REQUIRE(body.contains("# swap /dev/old"sv));
        REQUIRE(body.contains("swap /dev/sda3"sv));
    }
}

TEST_CASE("luks_swap::add_fstab_entry")
{
    install_silent_logger();

    SECTION("creates /etc/fstab when missing")
    {
        TempRoot root;
        REQUIRE(gucc::luks_swap::add_fstab_entry("swap"sv, root.path().string()));
        const auto body = gucc::file_utils::read_whole_file((root.path() / "etc" / "fstab").string());
        REQUIRE(body.contains("/dev/mapper/swap none swap"sv));
    }
    SECTION("replaces existing /dev/mapper/<name> line")
    {
        TempRoot root;
        fs::create_directories(root.path() / "etc");
        gucc::file_utils::create_file_for_overwrite(
            (root.path() / "etc" / "fstab").string(),
            "UUID=foo / btrfs defaults 0 0\n/dev/mapper/swap none swap pri=10 0 0\n"sv);

        REQUIRE(gucc::luks_swap::add_fstab_entry("swap"sv, root.path().string()));

        const auto body = gucc::file_utils::read_whole_file((root.path() / "etc" / "fstab").string());
        REQUIRE(body.contains("UUID=foo / btrfs defaults 0 0\n"sv));
        REQUIRE_FALSE(body.contains("pri=10"sv));
        REQUIRE(body.contains("/dev/mapper/swap none swap defaults 0 0\n"sv));
    }
}
