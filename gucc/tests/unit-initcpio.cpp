#include "doctest_compatibility.h"

#include "gucc/file_utils.hpp"
#include "gucc/initcpio.hpp"
#include "gucc/logger.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

static constexpr auto MKINITCPIO_STR = R"(
# MODULES
MODULES=()

# BINARIES
BINARIES=()

# FILES
FILES=()

# HOOKS
HOOKS=(base udev autodetect modconf block filesystems keyboard fsck)
)";

static constexpr auto MKINITCPIO_MODIFY_STR = R"(
# MODULES
MODULES=(radeon crc32c-intel)

# BINARIES
BINARIES=()

# FILES
FILES=()

# HOOKS
HOOKS=(base udev autodetect modconf block filesystems keyboard fsck btrfs usr lvm2 zfs)
)";

static constexpr auto MKINITCPIO_INVALID_TEST = R"(
# MODULES
MODULES=()

 (
# BINARIES
BINARIES=()
)
# FILES
FILES=()
()
# HOOKS
HOOKS=(base udev autodetect modconf block filesystems keyboard fsck

)";

static constexpr auto MKINITCPIO_TEST = R"(
# MODULES
MODULES=(radeon crc32c-intel)

# BINARIES
BINARIES=()

# FILES
FILES=()

# HOOKS
HOOKS=(base udev autodetect modconf block filesystems keyboard fsck btrfs usr lvm2 zfs)
)";

TEST_CASE("initcpio test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    using namespace gucc;  // NOLINT

    static constexpr std::string_view filename{"/tmp/mkinitcpio.conf"};

    SECTION("parse file")
    {
        // setup data.
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        auto initcpio = detail::Initcpio{filename};
        REQUIRE(initcpio.parse_file());

        const std::vector<std::string> default_hooks{
            "base", "udev", "autodetect", "modconf", "block", "filesystems", "keyboard", "fsck"};

        REQUIRE_EQ(initcpio.hooks.size(), default_hooks.size());
        REQUIRE_EQ(initcpio.hooks, default_hooks);
        REQUIRE(initcpio.modules.empty());
        REQUIRE(initcpio.files.empty());
    }
    SECTION("insert data")
    {
        // setup data.
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        auto initcpio = detail::Initcpio{filename};
        REQUIRE(initcpio.append_module("radeon"));
        REQUIRE(initcpio.append_hook("btrfs"));
        REQUIRE(initcpio.append_module("crc32c-intel"));
        REQUIRE(initcpio.append_hooks({"usr", "lvm2", "zfs"}));

        const std::vector<std::string> expected_hooks{
            "base", "udev", "autodetect", "modconf", "block", "filesystems", "keyboard", "fsck", "btrfs", "usr", "lvm2", "zfs"};

        const std::vector<std::string> expected_modules{
            "radeon", "crc32c-intel"};

        REQUIRE_EQ(initcpio.hooks.size(), expected_hooks.size());
        REQUIRE_EQ(initcpio.hooks, expected_hooks);
        REQUIRE_EQ(initcpio.modules.size(), expected_modules.size());
        REQUIRE_EQ(initcpio.modules, expected_modules);
        REQUIRE(initcpio.files.empty());

        // Write data.
        REQUIRE(initcpio.write());
    }
    SECTION("checking insertion of equal items")
    {
        // setup data.
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_MODIFY_STR));

        auto initcpio = detail::Initcpio{filename};
        REQUIRE(initcpio.parse_file());

        REQUIRE(!initcpio.append_module("radeon"));
        REQUIRE(!initcpio.append_hook("btrfs"));
        REQUIRE(!initcpio.append_module("crc32c-intel"));
        REQUIRE(!initcpio.insert_hook("btrfs", {"usr", "lvm2", "zfs"}));

        // Write data.
        REQUIRE(initcpio.write());
    }
    SECTION("check if file is equal to test data")
    {
        // setup data.
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_MODIFY_STR));

        // "\n# MODULES\nMODULES=(crc32c-intel)\n\n# BINARIES\nBINARIES=()\n\n# FILES\nFILES=()\n\n# HOOKS\nHOOKS=(base usr lvm2 zfs)"\n
        const auto& file_content = file_utils::read_whole_file(filename);
        REQUIRE_EQ(file_content, MKINITCPIO_TEST);
    }
    SECTION("check invalid file")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_INVALID_TEST));

        auto initcpio = detail::Initcpio{filename};
        REQUIRE(initcpio.parse_file());

        REQUIRE(initcpio.modules.empty());
        REQUIRE(initcpio.files.empty());
        REQUIRE(initcpio.hooks.empty());

        // Cleanup.
        fs::remove(filename);
    }
    SECTION("check empty file")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, ""));

        auto initcpio = detail::Initcpio{filename};
        REQUIRE(!initcpio.parse_file());
        REQUIRE(!initcpio.write());

        // Cleanup.
        fs::remove(filename);
    }
    SECTION("check write to nonexistent file")
    {
        auto initcpio = detail::Initcpio{"/path/to/non/exist_file.conf"};
        REQUIRE(!initcpio.parse_file());
        REQUIRE(!initcpio.write());
    }
    SECTION("check write to root file")
    {
        auto initcpio = detail::Initcpio{"/etc/mtab"};
        REQUIRE(!initcpio.parse_file());
        REQUIRE(!initcpio.write());
    }
}
