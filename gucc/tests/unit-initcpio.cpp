#include "doctest_compatibility.h"

#include "detail/initcpio_impl.hpp"
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

static constexpr auto MKINITCPIO_BASIC_CACHYOS_TEST = R"(
# MODULES
MODULES=()

# BINARIES
BINARIES=()

# FILES
FILES=()

# HOOKS
HOOKS=(base systemd autodetect microcode kms modconf block keyboard sd-vconsole plymouth filesystems)
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

        REQUIRE_EQ(initcpio.arrays().hooks.size(), default_hooks.size());
        REQUIRE_EQ(initcpio.arrays().hooks, default_hooks);
        REQUIRE(initcpio.arrays().modules.empty());
        REQUIRE(initcpio.arrays().files.empty());
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

        REQUIRE_EQ(initcpio.arrays().hooks.size(), expected_hooks.size());
        REQUIRE_EQ(initcpio.arrays().hooks, expected_hooks);
        REQUIRE_EQ(initcpio.arrays().modules.size(), expected_modules.size());
        REQUIRE_EQ(initcpio.arrays().modules, expected_modules);
        REQUIRE(initcpio.arrays().files.empty());

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

        REQUIRE(initcpio.arrays().modules.empty());
        REQUIRE(initcpio.arrays().files.empty());
        REQUIRE(initcpio.arrays().hooks.empty());

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
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

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
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("btrfs multi-device config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type       = gucc::fs::FilesystemType::Btrfs,
            .is_btrfs_multi_device = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "keymap", "consolefont", "btrfs", "filesystems"};
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

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
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("zfs + luks config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Zfs,
            .is_luks         = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "keyboard", "autodetect", "microcode", "kms", "modconf", "block",
            "keymap", "consolefont", "encrypt", "zfs", "filesystems"};
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("zfs + plymouth w/o encryption")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Zfs,
            .has_plymouth    = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "keymap", "consolefont", "plymouth", "zfs", "filesystems"};
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("zfs + native encryption w/o plymouth")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type  = gucc::fs::FilesystemType::Zfs,
            .has_plymouth     = true,
            .is_zfs_encrypted = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "keymap", "consolefont", "zfs", "filesystems"};
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("lvm only config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
            .is_lvm          = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "keymap", "consolefont", "lvm2", "filesystems", "fsck"};
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("luks only config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
            .is_luks         = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "keyboard", "autodetect", "microcode", "kms", "modconf", "block",
            "keymap", "consolefont", "encrypt", "filesystems", "fsck"};
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("lvm + luks config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
            .is_lvm          = true,
            .is_luks         = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "keyboard", "autodetect", "microcode", "kms", "modconf", "block",
            "keymap", "consolefont", "encrypt", "lvm2", "filesystems", "fsck"};
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("btrfs + luks config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Btrfs,
            .is_luks         = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "keyboard", "autodetect", "microcode", "kms", "modconf", "block",
            "keymap", "consolefont", "encrypt", "filesystems"};
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("ext4 with swap")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
            .has_swap        = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "keymap", "consolefont", "resume", "filesystems", "fsck"};
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("ext4 with plymouth")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
            .has_plymouth    = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "keymap", "consolefont", "plymouth", "filesystems", "fsck"};
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("systemd hook config")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type  = gucc::fs::FilesystemType::Ext4,
            .use_systemd_hook = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "systemd", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "sd-vconsole", "filesystems", "fsck"};
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("systemd hook with luks")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type  = gucc::fs::FilesystemType::Ext4,
            .is_luks          = true,
            .use_systemd_hook = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "systemd", "keyboard", "autodetect", "microcode", "kms", "modconf", "block",
            "sd-vconsole", "sd-encrypt", "filesystems", "fsck"};
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("systemd hook with luks + lvm + swap")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type  = gucc::fs::FilesystemType::Ext4,
            .is_lvm           = true,
            .is_luks          = true,
            .has_swap         = true,
            .use_systemd_hook = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "systemd", "keyboard", "autodetect", "microcode", "kms", "modconf", "block",
            "sd-vconsole", "sd-encrypt", "lvm2", "filesystems", "fsck"};
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("zfs forces non-systemd path")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type  = gucc::fs::FilesystemType::Zfs,
            .use_systemd_hook = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "keymap", "consolefont", "zfs", "filesystems"};
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("full config: luks + lvm + plymouth + swap")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type = gucc::fs::FilesystemType::Ext4,
            .is_lvm          = true,
            .is_luks         = true,
            .has_swap        = true,
            .has_plymouth    = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "udev", "keyboard", "autodetect", "microcode", "kms", "modconf", "block",
            "keymap", "consolefont", "plymouth", "encrypt", "lvm2",
            "resume", "filesystems", "fsck"};
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);

        ::fs::remove(filename);
    }
    SECTION("btrfs with systemd + plymouth")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, MKINITCPIO_STR));

        const auto config = initcpio::InitcpioConfig{
            .filesystem_type  = gucc::fs::FilesystemType::Btrfs,
            .has_plymouth     = true,
            .use_systemd_hook = true,
        };
        REQUIRE(initcpio::setup_initcpio_config(filename, config));

        auto result = detail::Initcpio{filename};
        REQUIRE(result.parse_file());

        const std::vector<std::string> expected_hooks{
            "base", "systemd", "autodetect", "microcode", "kms", "modconf", "block",
            "keyboard", "sd-vconsole", "plymouth", "filesystems"};
        REQUIRE_EQ(result.arrays().hooks, expected_hooks);
        const auto& file_content = file_utils::read_whole_file(filename);
        REQUIRE_EQ(file_content, MKINITCPIO_BASIC_CACHYOS_TEST);

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

TEST_CASE("parse_initcpio")
{
    using gucc::detail::parse_initcpio;
    using strings = std::vector<std::string>;

    SECTION("stock")
    {
        const auto arrays = parse_initcpio(MKINITCPIO_TEST);
        REQUIRE_EQ(arrays.modules, strings{"radeon", "crc32c-intel"});
        REQUIRE(arrays.files.empty());
        REQUIRE_EQ(arrays.hooks, strings{"base", "udev", "autodetect", "modconf", "block", "filesystems", "keyboard", "fsck", "btrfs", "usr", "lvm2", "zfs"});
    }
    SECTION("empty input and empty arrays")
    {
        const auto none = parse_initcpio("");
        REQUIRE(none.modules.empty());
        REQUIRE(none.files.empty());
        REQUIRE(none.hooks.empty());
        REQUIRE(parse_initcpio("HOOKS=()\n").hooks.empty());
        REQUIRE(parse_initcpio("HOOKS=(   )\n").hooks.empty());
    }
    SECTION("commented out are ignored")
    {
        const auto arrays = parse_initcpio("#HOOKS=(base udev)\n# HOOKS=(foo)\nHOOKS=(base systemd)\n");
        REQUIRE_EQ(arrays.hooks, strings{"base", "systemd"});
    }
    SECTION("trailing comment")
    {
        REQUIRE_EQ(parse_initcpio("HOOKS=(base udev) # keep udev\n").hooks, strings{"base", "udev"});
    }
    SECTION("hash inside a word is not a comment?")
    {
        REQUIRE_EQ(parse_initcpio("FILES=(/etc/a#b)\n").files, strings{"/etc/a#b"});
    }
    SECTION("quoted values are unquoted")
    {
        REQUIRE_EQ(parse_initcpio(R"x(MODULES=("radeon" 'i915' "nvme))x").modules, strings{"radeon", "i915", "\"nvme"});
    }
    SECTION("blanks and tabs")
    {
        REQUIRE_EQ(parse_initcpio("HOOKS=(  base\t\tudev   )\n").hooks, strings{"base", "udev"});
    }
    SECTION("multiline array")
    {
        const auto arrays = parse_initcpio("HOOKS=(\n  base\n  udev\n)\nFILES=(/etc/x)\n");
        REQUIRE(arrays.hooks.empty());
        REQUIRE_EQ(arrays.files, strings{"/etc/x"});
    }
    SECTION("missing closing paren")
    {
        const auto arrays = parse_initcpio("HOOKS=(base udev\nMODULES=(radeon)\n");
        REQUIRE(arrays.hooks.empty());
        REQUIRE_EQ(arrays.modules, strings{"radeon"});
        REQUIRE(parse_initcpio("HOOKS=(base udev").hooks.empty());
        REQUIRE(parse_initcpio("HOOKS=base").hooks.empty());
    }
    SECTION("text after closing paren ignored")
    {
        REQUIRE_EQ(parse_initcpio("HOOKS=(base udev)fsck keymap\n").hooks, strings{"base", "udev"});
    }
    SECTION("CRLF")
    {
        const auto arrays = parse_initcpio("MODULES=(radeon)\r\nHOOKS=(base udev)\r\n");
        REQUIRE_EQ(arrays.modules, strings{"radeon"});
        REQUIRE_EQ(arrays.hooks, strings{"base", "udev"});
    }
    SECTION("duplicate keys")
    {
        REQUIRE_EQ(parse_initcpio("HOOKS=(base)\nHOOKS=(base udev)\n").hooks, strings{"base", "udev"});
    }
    SECTION("similarly prefixed keys are not the array")
    {
        const auto arrays = parse_initcpio("MODULES=(radeon)\nMODULES_DECOMPRESS=\"yes\"\nFILESYSTEMS=(x)\n");
        REQUIRE_EQ(arrays.modules, strings{"radeon"});
        REQUIRE(arrays.files.empty());
    }
}

TEST_CASE("rewrite_initcpio")
{
    using gucc::detail::modify_initcpio_line;
    using gucc::detail::rewrite_initcpio;

    SECTION("modify_initcpio_line")
    {
        REQUIRE_EQ(modify_initcpio_line("MODULES=()", "radeon", "", "base"), "MODULES=(radeon)");
        REQUIRE_EQ(modify_initcpio_line("FILES=(/a)", "radeon", "/b", "base"), "FILES=(/b)");
        REQUIRE_EQ(modify_initcpio_line("HOOKS=(udev) # c", "radeon", "", "base udev"), "HOOKS=(base udev)");
        REQUIRE_EQ(modify_initcpio_line("#HOOKS=(udev)", "", "", "base"), "#HOOKS=(udev)");
        REQUIRE_EQ(modify_initcpio_line("MODULES_DECOMPRESS=\"yes\"", "radeon", "", ""), "MODULES_DECOMPRESS=\"yes\"");
        REQUIRE_EQ(modify_initcpio_line("BINARIES=()", "radeon", "", ""), "BINARIES=()");
    }
    SECTION("stock")
    {
        REQUIRE_EQ(rewrite_initcpio(MKINITCPIO_MODIFY_STR, "radeon crc32c-intel", "",
                       "base udev autodetect modconf block filesystems keyboard fsck btrfs usr lvm2 zfs"),
            MKINITCPIO_TEST);
    }
    SECTION("multiline array")
    {
        static constexpr auto content = "# hooks\nHOOKS=(\n  base\n  udev\n)\nCOMPRESSION=\"zstd\"\n";
        REQUIRE_EQ(rewrite_initcpio(content, "", "", "base systemd"), "# hooks\nHOOKS=(base systemd)\n  base\n  udev\n)\nCOMPRESSION=\"zstd\"\n");
    }
    SECTION("unterminated array")
    {
        REQUIRE_EQ(rewrite_initcpio("HOOKS=(base\nMODULES=()\n", "a", "", "b"), "HOOKS=(b)\nMODULES=(a)\n");
    }
    SECTION("indented rewritten")
    {
        REQUIRE_EQ(rewrite_initcpio("  FILES=(/a)\nBINARIES=()", "", "/c", ""), "FILES=(/c)\nBINARIES=()");
    }
    SECTION("every duplicate rewritten")
    {
        REQUIRE_EQ(rewrite_initcpio("HOOKS=(a)\nHOOKS=(b)", "", "", "c"), "HOOKS=(c)\nHOOKS=(c)");
    }
}
