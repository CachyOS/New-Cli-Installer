#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest_compatibility.h"

#include "installer_config.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST_CASE("installer config parsing")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);

    SECTION("empty config returns defaults")
    {
        auto result = installer::parse_installer_config(""sv);
        REQUIRE(result.has_value());

        auto& config = *result;
        REQUIRE_EQ(config.menus, 2);
        REQUIRE(!config.headless_mode);
        REQUIRE(!config.server_mode);
        REQUIRE(!config.device.has_value());
    }
    SECTION("valid complete config")
    {
        constexpr auto json = R"({
            "menus": 1,
            "headless_mode": true,
            "server_mode": false,
            "device": "/dev/nvme0n1",
            "fs_name": "btrfs",
            "partitions": [
                {
                    "name": "/dev/nvme0n1p1",
                    "mountpoint": "/boot",
                    "size": "512M",
                    "fs_name": "vfat",
                    "type": "boot"
                },
                {
                    "name": "/dev/nvme0n1p2",
                    "mountpoint": "/",
                    "size": "450G",
                    "type": "root"
                }
            ],
            "hostname": "cachyos",
            "locale": "en_US",
            "xkbmap": "us",
            "timezone": "America/New_York",
            "user_name": "testuser",
            "user_pass": "testpass",
            "user_shell": "/bin/bash",
            "root_pass": "rootpass",
            "kernel": "linux-cachyos",
            "desktop": "kde",
            "bootloader": "systemd-boot",
            "post_install": "/root/script.sh"
        })"sv;

        auto result = installer::parse_installer_config(json);
        REQUIRE(result.has_value());

        auto& config = *result;
        REQUIRE_EQ(config.menus, 1);
        REQUIRE(config.headless_mode);
        REQUIRE(!config.server_mode);
        REQUIRE_EQ(config.device, "/dev/nvme0n1"sv);
        REQUIRE_EQ(config.fs_name, "btrfs"sv);

        REQUIRE_EQ(config.partitions.size(), 2);
        REQUIRE_EQ(config.partitions[0].name, "/dev/nvme0n1p1"sv);
        REQUIRE_EQ(config.partitions[0].mountpoint, "/boot"sv);
        REQUIRE_EQ(config.partitions[0].fs_name, "vfat"sv);
        REQUIRE_EQ(config.partitions[0].type, installer::PartitionType::Boot);

        REQUIRE_EQ(config.partitions[1].name, "/dev/nvme0n1p2"sv);
        REQUIRE_EQ(config.partitions[1].fs_name, "btrfs"sv);
        REQUIRE_EQ(config.partitions[1].type, installer::PartitionType::Root);

        REQUIRE_EQ(config.hostname, "cachyos"sv);
        REQUIRE_EQ(config.locale, "en_US"sv);
        REQUIRE_EQ(config.xkbmap, "us"sv);
        REQUIRE_EQ(config.timezone, "America/New_York"sv);
        REQUIRE_EQ(config.user_name, "testuser"sv);
        REQUIRE_EQ(config.user_pass, "testpass"sv);
        REQUIRE_EQ(config.user_shell, "/bin/bash"sv);
        REQUIRE_EQ(config.root_pass, "rootpass"sv);
        REQUIRE_EQ(config.kernel, "linux-cachyos"sv);
        REQUIRE_EQ(config.desktop, "kde"sv);
        REQUIRE_EQ(config.bootloader, "systemd-boot"sv);
        REQUIRE_EQ(config.post_install, "/root/script.sh"sv);
    }
    SECTION("missing menus field returns error")
    {
        constexpr auto json = R"({"headless_mode": false})"sv;

        auto result = installer::parse_installer_config(json);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().contains("menus"sv));
    }
    SECTION("invalid JSON returns error")
    {
        constexpr auto json = R"({broken json)"sv;

        auto result = installer::parse_installer_config(json);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().contains("parse error"sv));
    }
    SECTION("invalid partition type returns error")
    {
        constexpr auto json = R"({
            "menus": 2,
            "fs_name": "ext4",
            "partitions": [
                {
                    "name": "/dev/sda1",
                    "mountpoint": "/",
                    "size": "100G",
                    "type": "invalid_type"
                }
            ]
        })"sv;

        auto result = installer::parse_installer_config(json);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().contains("Invalid partition type"sv));
    }
    SECTION("partition missing fs_name for non-root")
    {
        constexpr auto json = R"({
            "menus": 2,
            "fs_name": "ext4",
            "partitions": [
                {
                    "name": "/dev/sda1",
                    "mountpoint": "/boot",
                    "size": "512M",
                    "type": "boot"
                }
            ]
        })"sv;

        auto result = installer::parse_installer_config(json);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().contains("fs_name"sv));
    }
    SECTION("root partition inherits global fs_name")
    {
        constexpr auto json = R"({
            "menus": 2,
            "fs_name": "btrfs",
            "partitions": [
                {
                    "name": "/dev/sda2",
                    "mountpoint": "/",
                    "size": "100G",
                    "type": "root"
                }
            ]
        })"sv;

        auto result = installer::parse_installer_config(json);
        REQUIRE(result.has_value());
        REQUIRE_EQ(result->partitions.size(), 1);
        REQUIRE_EQ(result->partitions[0].fs_name, "btrfs"sv);
    }
    SECTION("server_mode parsing")
    {
        constexpr auto json = R"({
            "menus": 2,
            "server_mode": true
        })"sv;

        auto result = installer::parse_installer_config(json);
        REQUIRE(result.has_value());
        REQUIRE(result->server_mode);
    }
    SECTION("wrong type for headless_mode returns error")
    {
        constexpr auto json = R"({
            "menus": 2,
            "headless_mode": "yes"
        })"sv;

        auto result = installer::parse_installer_config(json);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().contains("headless_mode"sv));
        REQUIRE(result.error().contains("boolean"sv));
    }
    SECTION("partitions not array returns error")
    {
        constexpr auto json = R"({
            "menus": 2,
            "partitions": "not an array"
        })"sv;

        auto result = installer::parse_installer_config(json);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().contains("partitions"sv));
        REQUIRE(result.error().contains("array"sv));
    }
}

TEST_CASE("headless config validation")
{
    SECTION("non-headless mode always validates")
    {
        installer::InstallerConfig config{};
        config.headless_mode = false;

        auto result = installer::validate_headless_config(config);
        REQUIRE(result.has_value());
    }
    SECTION("headless mode with all fields validates")
    {
        installer::InstallerConfig config{};
        config.headless_mode = true;
        config.device = "/dev/sda"s;
        config.fs_name = "ext4"s;
        config.partitions.push_back({"/dev/sda1"s, "/"s, "100G"s, "ext4"s, installer::PartitionType::Root});
        config.hostname = "myhost"s;
        config.locale = "en_US"s;
        config.xkbmap = "us"s;
        config.timezone = "UTC"s;
        config.user_name = "user"s;
        config.user_pass = "pass"s;
        config.user_shell = "/bin/bash"s;
        config.root_pass = "rootpass"s;
        config.kernel = "linux"s;
        config.desktop = "kde"s;
        config.bootloader = "grub"s;

        auto result = installer::validate_headless_config(config);
        REQUIRE(result.has_value());
    }
    SECTION("headless mode missing device fails")
    {
        installer::InstallerConfig config{};
        config.headless_mode = true;
        config.fs_name = "ext4"s;
        config.partitions.push_back({"/dev/sda1"s, "/"s, "100G"s, "ext4"s, installer::PartitionType::Root});
        config.hostname = "myhost"s;
        config.locale = "en_US"s;
        config.xkbmap = "us"s;
        config.timezone = "UTC"s;
        config.user_name = "user"s;
        config.user_pass = "pass"s;
        config.user_shell = "/bin/bash"s;
        config.root_pass = "rootpass"s;
        config.kernel = "linux"s;
        config.desktop = "kde"s;
        config.bootloader = "grub"s;

        auto result = installer::validate_headless_config(config);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().contains("device"sv));
    }
    SECTION("headless mode missing user fields fails")
    {
        installer::InstallerConfig config{};
        config.headless_mode = true;
        config.device = "/dev/sda"s;
        config.fs_name = "ext4"s;
        config.partitions.push_back({"/dev/sda1"s, "/"s, "100G"s, "ext4"s, installer::PartitionType::Root});
        config.hostname = "myhost"s;
        config.locale = "en_US"s;
        config.xkbmap = "us"s;
        config.timezone = "UTC"s;
        config.root_pass = "rootpass"s;
        config.kernel = "linux"s;
        config.desktop = "kde"s;
        config.bootloader = "grub"s;

        auto result = installer::validate_headless_config(config);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().contains("user_name"sv));
    }
    SECTION("headless mode empty partitions fails")
    {
        installer::InstallerConfig config{};
        config.headless_mode = true;
        config.device = "/dev/sda"s;
        config.fs_name = "ext4"s;
        config.hostname = "myhost"s;
        config.locale = "en_US"s;
        config.xkbmap = "us"s;
        config.timezone = "UTC"s;
        config.user_name = "user"s;
        config.user_pass = "pass"s;
        config.user_shell = "/bin/bash"s;
        config.root_pass = "rootpass"s;
        config.kernel = "linux"s;
        config.desktop = "kde"s;
        config.bootloader = "grub"s;

        auto result = installer::validate_headless_config(config);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().contains("partitions"sv));
    }
}

TEST_CASE("partition type conversions")
{
    SECTION("string to partition type")
    {
        REQUIRE_EQ(installer::partition_type_from_string("root"), installer::PartitionType::Root);
        REQUIRE_EQ(installer::partition_type_from_string("boot"), installer::PartitionType::Boot);
        REQUIRE_EQ(installer::partition_type_from_string("additional"), installer::PartitionType::Additional);
        REQUIRE(!installer::partition_type_from_string("invalid").has_value());
    }
    SECTION("partition type to string")
    {
        REQUIRE_EQ(installer::partition_type_to_string(installer::PartitionType::Root), "root"sv);
        REQUIRE_EQ(installer::partition_type_to_string(installer::PartitionType::Boot), "boot"sv);
        REQUIRE_EQ(installer::partition_type_to_string(installer::PartitionType::Additional), "additional"sv);
    }
}

TEST_CASE("default config")
{
    auto config = installer::get_default_config();

    REQUIRE_EQ(config.menus, 2);
    REQUIRE(!config.headless_mode);
    REQUIRE(!config.server_mode);
    REQUIRE(!config.device.has_value());
    REQUIRE(!config.fs_name.has_value());
    REQUIRE(config.partitions.empty());
}
