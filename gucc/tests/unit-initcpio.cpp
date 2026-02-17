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
        ::fs::remove(filename);
    }
    SECTION("check empty file")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, ""));

        auto initcpio = detail::Initcpio{filename};
        REQUIRE(!initcpio.parse_file());
        REQUIRE(!initcpio.write());

        // Cleanup.
        ::fs::remove(filename);
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

TEST_CASE("setup initcpiocfg from config test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    using namespace gucc;  // NOLINT

    static constexpr std::string_view filename{"/tmp/mkinitcpio-setup.conf"};

    SECTION("default ext4 config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "keymap", "consolefont", "filesystems", "fsck"};
        REQUIRE_EQ(result.hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("btrfs config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Btrfs,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "keymap", "consolefont", "filesystems"};
        REQUIRE_EQ(result.hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("btrfs multi-device config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Btrfs,
            .is_btrfs_multi_device = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "keymap", "consolefont", "btrfs", "filesystems"};
        REQUIRE_EQ(result.hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("zfs config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Zfs,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "keymap", "consolefont", "zfs", "filesystems"};
        REQUIRE_EQ(result.hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("zfs + luks config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Zfs,
            .is_luks = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "keyboard", "autodetect", "microcode", "kms", "modconf", "block",
            "keymap", "consolefont", "encrypt", "zfs", "filesystems"};
        REQUIRE_EQ(result.hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("lvm only config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
            .is_lvm  = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "keymap", "consolefont", "lvm2", "filesystems", "fsck"};
        REQUIRE_EQ(result.hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("luks only config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
            .is_luks = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "keyboard", "autodetect", "microcode", "kms", "modconf", "block",
            "keymap", "consolefont", "encrypt", "filesystems", "fsck"};
        REQUIRE_EQ(result.hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("lvm + luks config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
            .is_lvm  = true,
            .is_luks = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "keyboard", "autodetect", "microcode", "kms", "modconf", "block",
            "keymap", "consolefont", "encrypt", "lvm2", "filesystems", "fsck"};
        REQUIRE_EQ(result.hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("btrfs + luks config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Btrfs,
            .is_luks = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "keyboard", "autodetect", "microcode", "kms", "modconf", "block",
            "keymap", "consolefont", "encrypt", "filesystems"};
        REQUIRE_EQ(result.hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("ext4 with swap")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
            .has_swap = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "keymap", "consolefont", "resume", "filesystems", "fsck"};
        REQUIRE_EQ(result.hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("ext4 with plymouth")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
            .has_plymouth = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "keymap", "consolefont", "plymouth", "filesystems", "fsck"};
        REQUIRE_EQ(result.hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("systemd hook config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
            .use_systemd_hook = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "systemd", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "sd-vconsole", "filesystems", "fsck"};
        REQUIRE_EQ(result.hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("systemd hook with luks")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
            .is_luks = true,
            .use_systemd_hook = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "systemd", "keyboard", "autodetect", "microcode", "kms", "modconf", "block",
            "sd-vconsole", "sd-encrypt", "filesystems", "fsck"};
        REQUIRE_EQ(result.hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("systemd hook with luks + lvm + swap")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
            .is_lvm  = true,
            .is_luks = true,
            .has_swap = true,
            .use_systemd_hook = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "systemd", "keyboard", "autodetect", "microcode", "kms", "modconf", "block",
            "sd-vconsole", "sd-encrypt", "lvm2", "filesystems", "fsck"};
        REQUIRE_EQ(result.hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("zfs forces non-systemd path")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Zfs,
            .use_systemd_hook = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "keymap", "consolefont", "zfs", "filesystems"};
        REQUIRE_EQ(result.hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("full config: luks + lvm + plymouth + swap")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
            .is_lvm  = true,
            .is_luks = true,
            .has_swap = true,
            .has_plymouth = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "keyboard", "autodetect", "microcode", "kms", "modconf", "block",
            "keymap", "consolefont", "plymouth", "encrypt", "lvm2",
            "resume", "filesystems", "fsck"};
        REQUIRE_EQ(result.hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("nonexistent file")
    {
        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
        };
        REQUIRE(!initcpio::setup_initcpio_config("/path/to/nonexistent.conf", config));
    }
}

