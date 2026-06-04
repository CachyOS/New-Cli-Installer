#pragma once

#include <cstdint>      // for uint64_t
#include <expected>     // for expected
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

/// @file partition_planner.hpp
/// @brief Headless, frontend-agnostic disk planning surface.
///
/// Frontends control the `PartitionPlan` through
/// this module's free functions and hand the final
namespace cachyos::installer::partition_planner {

/// One block device or partition presented to the user.
struct DeviceEntry {
    std::string name;
    std::string type;
    std::string fstype;
    std::string label;
    std::string model;
    std::uint64_t size_bytes{0};
    std::string parent;
    std::vector<std::string> mountpoints;
    std::string uuid;
    std::string partuuid;
};

/// Btrfs subvolume found on a currently-mounted btrfs filesystem.
struct ExistingBtrfsSubvolume {
    std::string device;
    std::string subvolume;
};

/// ZFS pool currently imported on the live system.
struct ExistingZfsPool {
    std::string name;
};

/// LVM volume group active on the live system.
struct ExistingLvmGroup {
    std::string name;
    std::string size;
};

/// One entry in a btrfs subvolume layout (canonical layout, or one suggested
/// for an existing filesystem the user is preserving).
struct BtrfsSubvolumeChoice {
    std::string subvolume;
    std::string mountpoint;
};

/// Snapshot of installer-relevant disk state.
struct DiskInventory {
    std::vector<DeviceEntry> block_devices;
    std::vector<ExistingBtrfsSubvolume> btrfs_subvolumes;
    std::vector<ExistingZfsPool> zfs_pools;
    std::vector<ExistingLvmGroup> lvm_groups;
};

/// Probe the live system for installer-relevant disk state.
[[nodiscard]] auto discover() noexcept -> DiskInventory;

/// CachyOS canonical btrfs subvolume layout.
[[nodiscard]] auto default_btrfs_layout() noexcept -> std::vector<BtrfsSubvolumeChoice>;

/// Suggest a mountpoint for a single subvolume path.
[[nodiscard]] auto suggest_mountpoint_for_subvolume(std::string_view subvol_path) noexcept
    -> std::string;

/// Inspect an unmounted btrfs partition: temp-mount read-only, list its
/// subvolumes, unmount.
[[nodiscard]] auto inspect_existing_btrfs(std::string_view device) noexcept
    -> std::expected<std::vector<std::string>, std::string>;

}  // namespace cachyos::installer::partition_planner
