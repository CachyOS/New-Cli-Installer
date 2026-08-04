#include "doctest_compatibility.h"

#include "gucc/logger.hpp"

#include "cachyos/installer_config.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

using cachyos::installer::InstallerConfig;
using cachyos::installer::parse_installer_config;
using cachyos::installer::validate_headless_config;

namespace {

auto valid_server_config() -> InstallerConfig {
    InstallerConfig cfg{};
    cfg.headless_mode        = true;
    cfg.server_mode          = true;
    cfg.allow_auto_partition = true;
    cfg.device               = "/dev/nvme0n1"s;
    cfg.fs_name              = "btrfs"s;
    cfg.hostname             = "srv"s;
    cfg.locale               = "en_US"s;
    cfg.xkbmap               = "us"s;
    cfg.timezone             = "UTC"s;
    cfg.user_name            = "admin"s;
    cfg.user_pass            = "x"s;
    cfg.user_shell           = "/bin/bash"s;
    cfg.root_pass            = "y"s;
    cfg.kernel               = "linux-cachyos-server"s;
    cfg.bootloader           = "systemd-boot"s;
    cfg.server_profile       = "web"s;
    cfg.ssh_authorized_keys  = {"ssh-ed25519 AAAA admin@host"s};
    return cfg;
}

}  // namespace

TEST_CASE("config parsing")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {});
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("rejected")
    {
        SECTION("typo")
        {
            auto cfg = parse_installer_config(R"({ "menus": 1, "keymapx": "us" })"sv);
            REQUIRE(!cfg.has_value());
            CHECK(cfg.error().contains("keymapx"));
        }
        SECTION("unknown")
        {
            auto cfg = parse_installer_config(R"({ "menus": 1, "totally_bogus": 5 })"sv);
            REQUIRE(!cfg.has_value());
        }
        SECTION("known parse")
        {
            auto cfg = parse_installer_config(R"({ "menus": 1, "desktop": "kde" })"sv);
            REQUIRE(cfg.has_value());
        }
    }
    SECTION("friendly aliases")
    {
        SECTION("basic")
        {
            auto cfg = parse_installer_config(R"({
                "menus": 1, "keymap": "de", "username": "bob",
                "user_password": "p", "root_password": "r"
            })"sv);
            REQUIRE(cfg.has_value());
            CHECK_EQ(cfg->xkbmap.value_or(""), "de");
            CHECK_EQ(cfg->user_name.value_or(""), "bob");
            CHECK_EQ(cfg->user_pass.value_or(""), "p");
            CHECK_EQ(cfg->root_pass.value_or(""), "r");
        }
        SECTION("key duplicated")
        {
            auto cfg = parse_installer_config(R"({ "menus": 1, "keymap": "de", "xkbmap": "us" })"sv);
            REQUIRE(!cfg.has_value());
        }
        SECTION("install types")
        {
            auto simple = parse_installer_config(R"({ "install_type": "simple" })"sv);
            REQUIRE(simple.has_value());
            CHECK_EQ(simple->menus, 1);
            auto advanced = parse_installer_config(R"({ "install_type": "advanced" })"sv);
            REQUIRE(advanced.has_value());
            CHECK_EQ(advanced->menus, 2);
            CHECK(!parse_installer_config(R"({ "install_type": "weird" })"sv).has_value());
            CHECK(!parse_installer_config(R"({ "menus": 1, "install_type": "simple" })"sv).has_value());
        }
    }
    SECTION("enums")
    {
        CHECK(!parse_installer_config(R"({ "menus": 1, "bootloader": "lilo" })"sv).has_value());
        CHECK(parse_installer_config(R"({ "menus": 1, "bootloader": "systemd-boot" })"sv).has_value());
        CHECK(!parse_installer_config(R"({ "menus": 1, "fs_name": "reiserfs" })"sv).has_value());
        CHECK(parse_installer_config(R"({ "menus": 1, "fs_name": "btrfs" })"sv).has_value());
        CHECK(parse_installer_config(R"({ "menus": 1, "fs_name": "zfs" })"sv).has_value());
        CHECK(!parse_installer_config(R"({ "menus": 1, "hw_clock": "martian" })"sv).has_value());
        CHECK(parse_installer_config(R"({ "menus": 1, "hw_clock": "localtime" })"sv).has_value());
    }
    SECTION("zfs")
    {
        auto encrypted = parse_installer_config(R"({ "menus": 1, "fs_name": "zfs", "zfs_passphrase": "hunter2" })"sv);
        REQUIRE(encrypted.has_value());
        REQUIRE(encrypted->zfs_passphrase.has_value());
        CHECK_EQ(*encrypted->zfs_passphrase, "hunter2"sv);

        CHECK(!parse_installer_config(R"({ "menus": 1, "fs_name": "ext4", "zfs_passphrase": "hunter2" })"sv).has_value());
        CHECK(!parse_installer_config(R"({ "menus": 1, "fs_name": "zfs", "subvolumes": [ { "subvolume": "@", "mountpoint": "/" } ] })"sv).has_value());
    }
    SECTION("headless")
    {
        auto cfg = parse_installer_config(R"({
            "menus": 1,
            "headless_mode": true,
            "allow_auto_partition": true,
            "device": "/dev/sda",
            "fs_name": "btrfs",
            "user_name": "admin",
            "user_pass": "x",
            "root_pass": "y",
            "desktop": "kde"
        })"sv);
        REQUIRE(cfg.has_value());
        CHECK_EQ(cfg->locale.value_or(""), "en_US.UTF-8");
        CHECK_EQ(cfg->xkbmap.value_or(""), "us");
        CHECK_EQ(cfg->timezone.value_or(""), "UTC");
        CHECK_EQ(cfg->user_shell.value_or(""), "/bin/bash");
        CHECK_EQ(cfg->hostname.value_or(""), "cachyos");
        CHECK_EQ(cfg->kernel.value_or(""), "linux-cachyos");

        CHECK(validate_headless_config(*cfg).has_value());
    }
    SECTION("optionals")
    {
        auto cfg = parse_installer_config(R"({
            "menus": 1,
            "autologin": true,
            "user_groups": ["wheel", "docker"],
            "hw_clock": "utc",
            "chwd": true,
            "carry_network": false,
            "os_prober": false,
            "netinstall_groups": ["Gaming Support", "Office Suite"]
        })"sv);
        REQUIRE(cfg.has_value());
        CHECK(cfg->autologin);
        CHECK_EQ(cfg->user_groups.size(), 2);
        CHECK_EQ(cfg->hw_clock.value_or(""), "utc");
        CHECK(cfg->chwd);
        CHECK(!cfg->carry_network);
        CHECK(!cfg->os_prober);
        CHECK_EQ(cfg->netinstall_groups.size(), 2);

        auto bare = parse_installer_config(R"({ "menus": 1 })"sv);
        REQUIRE(bare.has_value());
        CHECK(!bare->autologin);
        CHECK(bare->carry_network);
        CHECK(bare->os_prober);
        CHECK(bare->user_groups.empty());
        CHECK(bare->netinstall_groups.empty());
    }
    SECTION("examples")
    {
        for (const auto* path : {
                 "examples/desktop-btrfs.json",
                 "examples/desktop-zfs.json",
                 "examples/server-web.json",
                 "examples/server-db.json",
                 "examples/server-container-host.json",
                 "examples/server-cockpit.json",
             }) {
            CAPTURE(path);
            std::ifstream in{path};
            REQUIRE(in.good());
            std::stringstream buffer;
            buffer << in.rdbuf();

            auto cfg = parse_installer_config(buffer.str());
            REQUIRE(cfg.has_value());
            CHECK(validate_headless_config(*cfg).has_value());
        }
    }
    SECTION("server edition")
    {
        SECTION("deprecated server mode map")
        {
            auto cfg = parse_installer_config(R"({ "menus": 1, "server_mode": true })"sv);
            REQUIRE(cfg.has_value());
            CHECK(cfg->server_mode);
            REQUIRE(cfg->server_profile.has_value());
            CHECK_EQ(*cfg->server_profile, "minimal");
        }
        SECTION("derived server_mode")
        {
            auto cfg = parse_installer_config(R"({ "menus": 1, "server_profile": "cockpit" })"sv);
            REQUIRE(cfg.has_value());
            CHECK(cfg->server_mode);
            CHECK_EQ(*cfg->server_profile, "cockpit");
        }
        SECTION("server with disabled server mode")
        {
            auto cfg = parse_installer_config(R"({ "menus": 1, "server_mode": false, "server_profile": "web" })"sv);
            REQUIRE(!cfg.has_value());
        }
        SECTION("basic desktop conf")
        {
            auto cfg = parse_installer_config(R"({ "menus": 1, "desktop": "kde" })"sv);
            REQUIRE(cfg.has_value());
            CHECK(!cfg->server_mode);
            CHECK(!cfg->server_profile.has_value());
        }
        SECTION("extras")
        {
            auto cfg = parse_installer_config(R"({
                "menus": 1,
                "server_profile": "db",
                "ssh_authorized_keys": ["ssh-ed25519 AAAA a@b", "ssh-rsa BBBB c@d"],
                "server_extra_packages": ["htop", "tmux"],
                "server_extra_tcp_ports": [8080, 9000],
                "net_profiles_path": "file:///tmp/mine.toml"
            })"sv);
            REQUIRE(cfg.has_value());
            CHECK_EQ(cfg->ssh_authorized_keys.size(), 2);
            CHECK_EQ(cfg->server_extra_packages.size(), 2);
            REQUIRE_EQ(cfg->server_extra_tcp_ports.size(), 2);
            CHECK_EQ(cfg->server_extra_tcp_ports[0], 8080);
            REQUIRE(cfg->net_profiles_path.has_value());
        }
        SECTION("OFR port")
        {
            auto res = parse_installer_config(R"({ "menus": 1, "server_extra_tcp_ports": [70000] })"sv);
            REQUIRE(!res.has_value());
        }
        SECTION("non-string ssh")
        {
            auto res = parse_installer_config(R"({ "menus": 1, "ssh_authorized_keys": [123] })"sv);
            REQUIRE(!res.has_value());
        }
        SECTION("valid server conf")
        {
            auto result = validate_headless_config(valid_server_config());
            CHECK(result.has_value());
        }
        SECTION("server w/out de")
        {
            auto cfg    = valid_server_config();
            cfg.desktop = std::nullopt;
            auto result = validate_headless_config(cfg);
            CHECK(result.has_value());
        }
        SECTION("server with de")
        {
            auto cfg    = valid_server_config();
            cfg.desktop = "kde"s;
            auto result = validate_headless_config(cfg);
            REQUIRE(!result.has_value());
            CHECK(result.error().contains("desktop"));
        }
        SECTION("server without ssh keys")
        {
            auto cfg                = valid_server_config();
            cfg.ssh_authorized_keys = {};
            auto result             = validate_headless_config(cfg);
            REQUIRE(!result.has_value());
            CHECK(result.error().contains("ssh_authorized_keys"));
        }
        SECTION("desktop needs desktop")
        {
            auto cfg           = valid_server_config();
            cfg.server_profile = std::nullopt;
            cfg.server_mode    = false;
            cfg.desktop        = std::nullopt;
            auto result        = validate_headless_config(cfg);
            REQUIRE(!result.has_value());
            CHECK(result.error().contains("desktop"));
        }
    }
}
