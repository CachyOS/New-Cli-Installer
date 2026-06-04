#include "cachyos/partition_planner.hpp"
#include "cachyos/disk.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/btrfs_query.hpp"
#include "gucc/lvm.hpp"
#include "gucc/zfs_query.hpp"

#include <array>
#include <string_view>
#include <utility>

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

}  // namespace cachyos::installer::partition_planner
