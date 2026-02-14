#include "doctest_compatibility.h"

#include "gucc/systemd_homed.hpp"
#include "gucc/logger.hpp"

#include <algorithm>
#include <ranges>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

TEST_CASE("type_to_string")
{
    using gucc::homed::StorageType;
    using gucc::homed::LuksFilesystemType;

    SECTION("luks storage")
    {
        REQUIRE_EQ(gucc::homed::storage_type_to_string(StorageType::Luks), "luks"sv);
    }
    SECTION("fscrypt storage")
    {
        REQUIRE_EQ(gucc::homed::storage_type_to_string(StorageType::Fscrypt), "fscrypt"sv);
    }
    SECTION("directory storage")
    {
        REQUIRE_EQ(gucc::homed::storage_type_to_string(StorageType::Directory), "directory"sv);
    }
    SECTION("subvolume storage")
    {
        REQUIRE_EQ(gucc::homed::storage_type_to_string(StorageType::Subvolume), "subvolume"sv);
    }
    SECTION("cifs storage")
    {
        REQUIRE_EQ(gucc::homed::storage_type_to_string(StorageType::Cifs), "cifs"sv);
    }
    SECTION("ext4 filesystem")
    {
        REQUIRE_EQ(gucc::homed::luks_fs_type_to_string(LuksFilesystemType::Ext4), "ext4"sv);
    }
    SECTION("btrfs filesystem")
    {
        REQUIRE_EQ(gucc::homed::luks_fs_type_to_string(LuksFilesystemType::Btrfs), "btrfs"sv);
    }
    SECTION("xfs filesystem")
    {
        REQUIRE_EQ(gucc::homed::luks_fs_type_to_string(LuksFilesystemType::Xfs), "xfs"sv);
    }
}

TEST_CASE("generate args from config")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    using gucc::homed::HomedUserConfig;
    using gucc::homed::StorageType;
    using gucc::homed::LuksFilesystemType;

    SECTION("basic")
    {
        HomedUserConfig config{
            .username = "testuser"sv,
            .password = "secret123"sv,
        };

        const auto args = generate_homectl_args(config);
        REQUIRE(std::ranges::contains(args, "--storage=luks"));
        REQUIRE(std::ranges::contains(args, "--fs-type=ext4"));
        REQUIRE(std::ranges::contains(args, "--luks-discard=false"));
        REQUIRE(std::ranges::contains(args, "--luks-offline-discard=true"));
    }
    SECTION("with all options")
    {
        HomedUserConfig config{
            .username      = "testuser"sv,
            .password      = "secret123"sv,
            .real_name     = "Test User"sv,
            .shell         = "/usr/bin/zsh"sv,
            .uid           = 60100,
            .groups        = {"wheel", "audio", "video"},
            .storage       = StorageType::Luks,
            .image_path    = "/custom/path/testuser.home",
            .home_dir      = "/home/testuser",
            .luks_fs_type  = LuksFilesystemType::Btrfs,
            .disk_size     = "10G",
            .luks_discard  = true,
            .luks_offline_discard = true,
        };

        const auto args = generate_homectl_args(config);
        REQUIRE(std::ranges::contains(args, "--storage=luks"));
        REQUIRE(std::ranges::contains(args, "--real-name=Test User"));
        REQUIRE(std::ranges::contains(args, "--shell=/usr/bin/zsh"));
        REQUIRE(std::ranges::contains(args, "--uid=60100"));
        REQUIRE(std::ranges::contains(args, "--member-of=wheel,audio,video"));
        REQUIRE(std::ranges::contains(args, "--image-path=/custom/path/testuser.home"));
        REQUIRE(std::ranges::contains(args, "--home-dir=/home/testuser"));
        REQUIRE(std::ranges::contains(args, "--fs-type=btrfs"));
        REQUIRE(std::ranges::contains(args, "--disk-size=10G"));
        REQUIRE(std::ranges::contains(args, "--luks-discard=true"));
        REQUIRE(std::ranges::contains(args, "--luks-offline-discard=true"));
    }
    SECTION("with directory storage")
    {
        HomedUserConfig config{
            .username = "testuser"sv,
            .password = "secret123"sv,
            .storage  = StorageType::Directory,
        };

        const auto args = generate_homectl_args(config);
        REQUIRE(std::ranges::contains(args, "--storage=directory"));
        REQUIRE(!std::ranges::contains(args, "--fs-type=ext4"));
        REQUIRE(!std::ranges::contains(args, "--luks-discard=false"));
    }
    SECTION("with fscrypt storage")
    {
        HomedUserConfig config{
            .username = "testuser"sv,
            .password = "secret123"sv,
            .storage  = StorageType::Fscrypt,
        };

        const auto args = generate_homectl_args(config);
        REQUIRE(std::ranges::contains(args, "--storage=fscrypt"));
        REQUIRE(!std::ranges::contains(args, "--fs-type=ext4"));
    }
    SECTION("with subvolume storage")
    {
        HomedUserConfig config{
            .username = "testuser"sv,
            .password = "secret123"sv,
            .storage  = StorageType::Subvolume,
        };

        const auto args = generate_homectl_args(config);
        REQUIRE(std::ranges::contains(args, "--storage=subvolume"));
    }
    SECTION("with cifs storage")
    {
        HomedUserConfig config{
            .username = "testuser"sv,
            .password = "secret123"sv,
            .storage  = StorageType::Cifs,
        };

        const auto args = generate_homectl_args(config);
        REQUIRE(std::ranges::contains(args, "--storage=cifs"));
    }
    SECTION("with single group")
    {
        HomedUserConfig config{
            .username = "admin"sv,
            .password = "password"sv,
            .groups   = {"wheel"},
            .storage  = StorageType::Directory,
        };

        const auto args = generate_homectl_args(config);
        REQUIRE(std::ranges::contains(args, "--member-of=wheel"));
    }
    SECTION("with xfs filesystem")
    {
        HomedUserConfig config{
            .username     = "testuser"sv,
            .password     = "secret123"sv,
            .storage      = StorageType::Luks,
            .luks_fs_type = LuksFilesystemType::Xfs,
        };

        const auto args = generate_homectl_args(config);
        REQUIRE(std::ranges::contains(args, "--fs-type=xfs"));
    }
    SECTION("default values")
    {
        HomedUserConfig config{
            .username = "testuser"sv,
            .password = "secret"sv,
        };

        REQUIRE_EQ(config.storage, StorageType::Luks);
        REQUIRE_EQ(config.luks_fs_type, LuksFilesystemType::Ext4);
        REQUIRE_EQ(config.luks_discard, false);
        REQUIRE_EQ(config.luks_offline_discard, true);
        REQUIRE(config.real_name.empty());
        REQUIRE(config.shell.empty());
        REQUIRE_FALSE(config.uid.has_value());
        REQUIRE(config.groups.empty());
        REQUIRE_FALSE(config.image_path.has_value());
        REQUIRE_FALSE(config.home_dir.has_value());
        REQUIRE_FALSE(config.disk_size.has_value());
    }
}
