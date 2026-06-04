#include "cachyos/partition_planner.hpp"
#include "cachyos/disk.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/btrfs_query.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/luks.hpp"
#include "gucc/lvm.hpp"
#include "gucc/zfs_query.hpp"

#include <array>
#include <filesystem>
#include <random>
#include <string_view>
#include <system_error>
#include <utility>

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

// NOLINTNEXTLINE
using namespace cachyos::installer::partition_planner;

auto block_devices_inventory() noexcept -> std::vector<DeviceEntry> {
    auto devs = gucc::disk::list_block_devices();
    if (!devs) {
        return {};
    }

    std::vector<DeviceEntry> out;
    out.reserve(devs->size());
    for (auto& d : *devs) {
        out.push_back(DeviceEntry{
            .name        = std::move(d.name),
            .type        = std::move(d.type),
            .fstype      = std::move(d.fstype),
            .label       = d.label.value_or(""),
            .model       = d.model.value_or(""),
            .size_bytes  = d.size.value_or(0),
            .parent      = d.pkname.value_or(""),
            .mountpoints = std::move(d.mountpoints),
            .uuid        = std::move(d.uuid),
            .partuuid    = d.partuuid.value_or(""),
        });
    }
    return out;
}

auto btrfs_subvolumes_inventory(const std::vector<DeviceEntry>& devices) noexcept
    -> std::vector<ExistingBtrfsSubvolume> {
    std::vector<ExistingBtrfsSubvolume> out;
    for (const auto& dev : devices) {
        if (dev.fstype != "btrfs"sv) {
            continue;
        }
        for (const auto& mp : dev.mountpoints) {
            for (auto& subvol : gucc::fs::list_btrfs_subvolumes(mp, ""sv)) {
                out.push_back(ExistingBtrfsSubvolume{
                    .device    = dev.name,
                    .subvolume = std::move(subvol.subvolume),
                });
            }
        }
    }
    return out;
}

auto zfs_pools_inventory() noexcept -> std::vector<ExistingZfsPool> {
    std::vector<ExistingZfsPool> out;
    for (auto& pool : gucc::fs::list_zfs_pools()) {
        out.push_back(ExistingZfsPool{
            .name = std::move(pool.name),
        });
    }
    return out;
}

auto lvm_groups_inventory() noexcept -> std::vector<ExistingLvmGroup> {
    std::vector<ExistingLvmGroup> out;
    for (auto& [name, size] : gucc::lvm::show_volume_groups()) {
        out.push_back(ExistingLvmGroup{
            .name = std::move(name),
            .size = std::move(size),
        });
    }
    return out;
}

[[nodiscard]] auto validate_luks_inputs(std::string_view device,
    std::string_view mapper_name,
    std::string_view passphrase) noexcept
    -> std::expected<void, std::string> {
    if (device.empty()) {
        return std::unexpected(std::string{"device is empty"});
    }
    if (mapper_name.empty()) {
        return std::unexpected(std::string{"mapper_name is empty"});
    }
    if (passphrase.empty()) {
        return std::unexpected(std::string{"passphrase is empty"});
    }
    return {};
}

}  // namespace

namespace cachyos::installer::partition_planner {

auto discover() noexcept -> DiskInventory {
    DiskInventory inv{};
    inv.block_devices    = block_devices_inventory();
    inv.btrfs_subvolumes = btrfs_subvolumes_inventory(inv.block_devices);
    inv.zfs_pools        = zfs_pools_inventory();
    inv.lvm_groups       = lvm_groups_inventory();
    return inv;
}

auto default_btrfs_layout() noexcept -> std::vector<BtrfsSubvolumeChoice> {
    auto src = cachyos::installer::default_btrfs_subvolumes();
    std::vector<BtrfsSubvolumeChoice> out;
    out.reserve(src.size());
    for (auto& subvol : src) {
        out.push_back(BtrfsSubvolumeChoice{
            .subvolume  = std::move(subvol.subvolume),
            .mountpoint = std::move(subvol.mountpoint),
        });
    }
    return out;
}

auto suggest_mountpoint_for_subvolume(std::string_view subvol_path) noexcept -> std::string {
    const auto name = subvol_path.starts_with('/') ? subvol_path.substr(1) : subvol_path;
    if (name.empty()) {
        return {};
    }

    static constexpr std::array<std::pair<std::string_view, std::string_view>, 8> kKnown{{
        {"@"sv, "/"sv},
        {"@home"sv, "/home"sv},
        {"@root"sv, "/root"sv},
        {"@srv"sv, "/srv"sv},
        {"@cache"sv, "/var/cache"sv},
        {"@tmp"sv, "/var/tmp"sv},
        {"@log"sv, "/var/log"sv},
        {"@snapshots"sv, "/.snapshots"sv},
    }};
    for (const auto& [subvolname, subvolpath] : kKnown) {
        if (name == subvolname) {
            return std::string{subvolpath};
        }
    }

    // Unknown subvolume. /@
    const auto stem = name.starts_with('@') ? name.substr(1) : name;
    std::string out{"/"};
    out.append(stem);
    return out;
}

auto inspect_existing_btrfs(std::string_view device) noexcept
    -> std::expected<std::vector<std::string>, std::string> {
    if (device.empty()) {
        return std::unexpected(std::string{"device is empty"});
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    const auto scratch = fs::temp_directory_path() / fmt::format("cachyos-btrfs-inspect-{}", std::random_device{}());

    fs::create_directories(scratch, ec);
    if (ec) {
        return std::unexpected(fmt::format("failed to create scratch dir: {}", ec.message()));
    }

    // discovery only, never modify the source FS.
    const auto mount_cmd = fmt::format(FMT_COMPILE("mount -t btrfs -o ro '{}' '{}' 2>/dev/null"),
        device, scratch.string());
    if (!gucc::utils::exec_checked(mount_cmd)) {
        fs::remove_all(scratch, ec);
        return std::unexpected(fmt::format("failed to mount {} for inspection", device));
    }

    auto subvols = gucc::fs::list_btrfs_subvolumes(scratch.string(), ""sv);

    // a left-over mount is recoverable
    const auto umount_cmd = fmt::format(FMT_COMPILE("umount '{}' 2>/dev/null"), scratch.string());
    if (!gucc::utils::exec_checked(umount_cmd)) {
        spdlog::warn("inspect_existing_btrfs: failed to unmount scratch at {}", scratch.string());
    }
    fs::remove_all(scratch, ec);

    std::vector<std::string> paths;
    paths.reserve(subvols.size());
    for (auto& subvol : subvols) {
        paths.push_back(std::move(subvol.subvolume));
    }
    return paths;
}

auto default_zfs_layout(std::string_view zpool_name) noexcept
    -> std::vector<ZfsDatasetChoice> {
    auto src = cachyos::installer::default_zfs_datasets(zpool_name);
    std::vector<ZfsDatasetChoice> out;
    out.reserve(src.size());
    for (auto& dataset : src) {
        out.push_back(ZfsDatasetChoice{
            .dataset    = std::move(dataset.zpath),
            .mountpoint = std::move(dataset.mountpoint),
        });
    }
    return out;
}

auto prepare_default_zpool(std::string_view partition,
    std::string_view zpool_name,
    std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    auto res = cachyos::installer::zfs_auto_pres(partition, zpool_name, mountpoint);
    if (!res) {
        return std::unexpected(std::move(res).error());
    }
    return {};
}

auto encrypt_partition(const LuksFormatRequest& req) noexcept
    -> std::expected<void, std::string> {
    if (auto v = validate_luks_inputs(req.device, req.mapper_name, req.passphrase); !v) {
        return v;
    }
    if (!gucc::crypto::luks1_format(req.passphrase, req.device, req.extra_flags)) {
        return std::unexpected(fmt::format("failed to format LUKS partition {}", req.device));
    }
    if (!gucc::crypto::luks1_open(req.passphrase, req.device, req.mapper_name)) {
        return std::unexpected(fmt::format("failed to open LUKS partition {} as {}",
            req.device, req.mapper_name));
    }
    return {};
}

auto open_encrypted_partition(const LuksOpenRequest& req) noexcept
    -> std::expected<void, std::string> {
    if (auto v = validate_luks_inputs(req.device, req.mapper_name, req.passphrase); !v) {
        return v;
    }
    if (!gucc::crypto::luks1_open(req.passphrase, req.device, req.mapper_name)) {
        return std::unexpected(fmt::format("failed to open LUKS partition {} as {}",
            req.device, req.mapper_name));
    }
    return {};
}

}  // namespace cachyos::installer::partition_planner
