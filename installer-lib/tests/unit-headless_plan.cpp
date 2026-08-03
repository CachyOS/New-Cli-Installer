#include "doctest_compatibility.h"

#include "gucc/logger.hpp"

#include "cachyos/headless_plan.hpp"
#include "cachyos/installer_config.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <variant>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

using cachyos::installer::headless_strategy_from_config;
using cachyos::installer::InstallerConfig;
using cachyos::installer::PartitionConfig;
using cachyos::installer::PartitionType;
using cachyos::installer::SubvolumeConfig;
namespace strategy = cachyos::installer::partition_strategy;

namespace {

[[nodiscard]] auto valid_uefi_config() -> InstallerConfig {
    InstallerConfig cfg{};
    cfg.headless_mode = true;
    cfg.device        = "/dev/nvme0n1"s;
    cfg.fs_name       = "btrfs"s;
    cfg.partitions    = {
        PartitionConfig{.name = "/dev/nvme0n1p1"s, .mountpoint = "/boot"s, .size = "512M"s, .fs_name = "vfat"s, .type = PartitionType::Boot},
        PartitionConfig{.name = "/dev/nvme0n1p2"s, .mountpoint = "/"s, .size = "450G"s, .fs_name = "btrfs"s, .type = PartitionType::Root},
    };
    return cfg;
}

[[nodiscard]] auto joined_errors(const std::vector<std::string>& errors) -> std::string {
    std::string joined{};
    for (const auto& error : errors) {
        joined += error;
        joined += '\n';
    }
    return joined;
}

[[nodiscard]] auto contains(std::string_view haystack, std::string_view needle) -> bool {
    return haystack.find(needle) != std::string_view::npos;
}

constexpr auto SAMPLE_SETTINGS = R"({
    "menus": 1,
    "headless_mode": true,
    "device": "/dev/nvme0n1",
    "fs_name": "btrfs",
    "partitions": [
        { "name": "/dev/nvme0n1p2", "mountpoint": "/", "size": "450G", "type": "root" },
        { "name": "/dev/nvme0n1p1", "mountpoint": "/boot", "size": "512M", "fs_name": "vfat", "type": "boot" }
    ],
    "hostname": "cachyos",
    "locale": "en_US",
    "xkbmap": "us",
    "timezone": "America/New_York",
    "user_name": "testuser",
    "user_pass": "test",
    "user_shell": "/bin/bash",
    "root_pass": "secure",
    "kernel": "linux-cachyos",
    "desktop": "kde",
    "bootloader": "systemd-boot"
})"sv;

}  // namespace

TEST_CASE("headless partitioning")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("example settings.json")
    {
        const auto parsed = cachyos::installer::parse_installer_config(SAMPLE_SETTINGS);
        REQUIRE(parsed.has_value());

        const auto strategy = headless_strategy_from_config(*parsed, true);
        REQUIRE(strategy.has_value());

        const auto* layout = std::get_if<strategy::CreateLayout>(&*strategy);
        REQUIRE(layout != nullptr);
        REQUIRE_EQ(layout->device, "/dev/nvme0n1"sv);
        REQUIRE_EQ(layout->partitions.size(), 2);

        REQUIRE_EQ(layout->partitions[0].device, "/dev/nvme0n1p1"sv);
        REQUIRE_EQ(layout->partitions[0].fstype, "vfat"sv);
        REQUIRE_EQ(layout->partitions[0].mountpoint, "/boot"sv);
        REQUIRE_EQ(layout->partitions[0].size, "512M"sv);

        REQUIRE_EQ(layout->partitions[1].device, "/dev/nvme0n1p2"sv);
        REQUIRE_EQ(layout->partitions[1].fstype, "btrfs"sv);
        REQUIRE_EQ(layout->partitions[1].mountpoint, "/"sv);
        REQUIRE_EQ(layout->partitions[1].size, "450G"sv);

        REQUIRE_FALSE(layout->btrfs_subvolumes.empty());
    }
    SECTION("a gap in the declared partition numbers is rejected")
    {
        auto cfg                 = valid_uefi_config();
        cfg.partitions[1].name   = "/dev/nvme0n1p3"s;
        const auto strategy      = headless_strategy_from_config(cfg, true);
        REQUIRE_FALSE(strategy.has_value());
        REQUIRE(contains(joined_errors(strategy.error()), "without gaps"sv));
    }
    SECTION("a duplicated partition number is rejected")
    {
        auto cfg               = valid_uefi_config();
        cfg.partitions[1].name = "/dev/nvme0n1p1"s;
        const auto strategy    = headless_strategy_from_config(cfg, true);
        REQUIRE_FALSE(strategy.has_value());
        REQUIRE(contains(joined_errors(strategy.error()), "declared twice"sv));
    }
    SECTION("a name that belongs to another disk is rejected")
    {
        auto cfg               = valid_uefi_config();
        cfg.partitions[0].name = "/dev/sda1"s;
        const auto strategy    = headless_strategy_from_config(cfg, true);
        REQUIRE_FALSE(strategy.has_value());
        REQUIRE(contains(joined_errors(strategy.error()), "is not partition 1 of device"sv));
    }
    SECTION("all problems are reported in one pass")
    {
        InstallerConfig cfg{};
        cfg.headless_mode = true;
        cfg.device        = "/dev/sda"s;
        cfg.fs_name       = "btrfs"s;
        cfg.partitions    = {
            PartitionConfig{.name = "/dev/sda1"s, .mountpoint = "/"s, .size = "100G"s, .fs_name = "btrfs"s, .type = PartitionType::Root},
            PartitionConfig{.name = "/dev/sda2"s, .mountpoint = "/"s, .size = "100G"s, .fs_name = "reiserfs"s, .type = PartitionType::Root},
        };

        const auto strategy = headless_strategy_from_config(cfg, true);
        REQUIRE_FALSE(strategy.has_value());

        const auto errors = joined_errors(strategy.error());
        REQUIRE(contains(errors, "exactly one partition of type 'root', found 2"sv));
        REQUIRE(contains(errors, "needs exactly one partition of type 'boot', found 0"sv));
        REQUIRE(contains(errors, "unknown filesystem 'reiserfs'"sv));
        REQUIRE(contains(errors, "both claim mountpoint '/'"sv));
    }
    SECTION("a root 100\% accepted")
    {
        auto cfg                = valid_uefi_config();
        cfg.partitions[1].size  = "100%"s;

        const auto strategy = headless_strategy_from_config(cfg, true);
        REQUIRE(strategy.has_value());
        REQUIRE(std::holds_alternative<strategy::CreateLayout>(*strategy));
    }
    SECTION("rejected percentage")
    {
        auto cfg                = valid_uefi_config();
        cfg.partitions[1].size  = "50%"s;

        const auto strategy = headless_strategy_from_config(cfg, true);
        REQUIRE_FALSE(strategy.has_value());
        REQUIRE(contains(joined_errors(strategy.error()), "size '50%' is not supported"sv));
    }
    SECTION("a UEFI install without an ESP is rejected")
    {
        auto cfg = valid_uefi_config();
        cfg.partitions.erase(cfg.partitions.begin());
        cfg.partitions[0].name = "/dev/nvme0n1p1"s;

        const auto strategy = headless_strategy_from_config(cfg, true);
        REQUIRE_FALSE(strategy.has_value());
        REQUIRE(contains(joined_errors(strategy.error()), "type 'boot', found 0"sv));
    }
    SECTION("an empty layout never reaches EraseAndAuto without the opt-in")
    {
        auto cfg = valid_uefi_config();
        cfg.partitions.clear();

        const auto strategy = headless_strategy_from_config(cfg, true);
        REQUIRE_FALSE(strategy.has_value());
        REQUIRE(contains(joined_errors(strategy.error()), "allow_auto_partition"sv));
    }
    SECTION("allow_auto_partition is the only route to EraseAndAuto")
    {
        auto cfg                 = valid_uefi_config();
        cfg.partitions.clear();
        cfg.allow_auto_partition = true;

        const auto strategy = headless_strategy_from_config(cfg, true);
        REQUIRE(strategy.has_value());

        const auto* erase = std::get_if<strategy::EraseAndAuto>(&*strategy);
        REQUIRE(erase != nullptr);
        REQUIRE_EQ(erase->device, "/dev/nvme0n1"sv);
    }
    SECTION("a declared layout wins over allow_auto_partition")
    {
        auto cfg                 = valid_uefi_config();
        cfg.allow_auto_partition = true;

        const auto strategy = headless_strategy_from_config(cfg, true);
        REQUIRE(strategy.has_value());
        REQUIRE(std::holds_alternative<strategy::CreateLayout>(*strategy));
    }
    SECTION("explicit subvolumes replace the defaults")
    {
        auto cfg                    = valid_uefi_config();
        cfg.use_default_subvolumes  = false;
        cfg.subvolumes              = {
            SubvolumeConfig{.subvolume = "@"s, .mountpoint = "/"s},
            SubvolumeConfig{.subvolume = "@home"s, .mountpoint = "/home"s},
        };

        const auto strategy = headless_strategy_from_config(cfg, true);
        REQUIRE(strategy.has_value());

        const auto* layout = std::get_if<strategy::CreateLayout>(&*strategy);
        REQUIRE(layout != nullptr);
        REQUIRE_EQ(layout->btrfs_subvolumes.size(), 2);
        REQUIRE_EQ(layout->btrfs_subvolumes[0].subvolume, "@"sv);
        REQUIRE_EQ(layout->btrfs_subvolumes[1].mountpoint, "/home"sv);
    }
    SECTION("no subvolumes are invented for a non-btrfs root")
    {
        auto cfg                  = valid_uefi_config();
        cfg.fs_name               = "ext4"s;
        cfg.partitions[1].fs_name = "ext4"s;

        const auto strategy = headless_strategy_from_config(cfg, true);
        REQUIRE(strategy.has_value());

        const auto* layout = std::get_if<strategy::CreateLayout>(&*strategy);
        REQUIRE(layout != nullptr);
        REQUIRE(layout->btrfs_subvolumes.empty());
    }
    SECTION("subvolumes on a non-btrfs root are rejected rather than ignored")
    {
        auto cfg                  = valid_uefi_config();
        cfg.fs_name               = "ext4"s;
        cfg.partitions[1].fs_name = "ext4"s;

        cfg.subvolumes = {SubvolumeConfig{.subvolume = "@"s, .mountpoint = "/"s}};

        const auto strategy = headless_strategy_from_config(cfg, true);
        REQUIRE_FALSE(strategy.has_value());
        REQUIRE(contains(joined_errors(strategy.error()), "requires a btrfs root filesystem"sv));
    }
    SECTION("mount_opts override the per-filesystem defaults")
    {
        auto cfg        = valid_uefi_config();
        cfg.mount_opts  = "noatime,compress=zstd"s;

        const auto strategy = headless_strategy_from_config(cfg, true);
        REQUIRE(strategy.has_value());

        const auto* layout = std::get_if<strategy::CreateLayout>(&*strategy);
        REQUIRE(layout != nullptr);
        REQUIRE(std::ranges::all_of(layout->partitions,
            [](const auto& part) { return part.mount_opts == "noatime,compress=zstd"sv; }));
    }
    SECTION("a missing device is rejected")
    {
        auto cfg   = valid_uefi_config();
        cfg.device = std::nullopt;

        const auto strategy = headless_strategy_from_config(cfg, true);
        REQUIRE_FALSE(strategy.has_value());
        REQUIRE(contains(joined_errors(strategy.error()), "'device' is required"sv));
    }
    SECTION("a BIOS install does not require an ESP")
    {
        auto cfg = valid_uefi_config();
        cfg.partitions.erase(cfg.partitions.begin());
        cfg.partitions[0].name = "/dev/nvme0n1p1"s;

        const auto strategy = headless_strategy_from_config(cfg, false);
        REQUIRE(strategy.has_value());
        REQUIRE(std::holds_alternative<strategy::CreateLayout>(*strategy));
    }
}
