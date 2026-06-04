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
        if (dev.fstype != "btrfs") {
            continue;
        }
        for (const auto& mp : dev.mountpoints) {
            for (auto& subvol : gucc::fs::list_btrfs_subvolumes(mp, "")) {
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

}  // namespace cachyos::installer::partition_planner
