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

/// One entry in a ZFS dataset layout.
struct ZfsDatasetChoice {
    /// Dataset path.
    std::string dataset;
    /// Mountpoint or the literal "none"
    /// for container datasets that should not be mounted.
    std::string mountpoint;
};

/// CachyOS canonical ZFS dataset layout for a given zpool name.
[[nodiscard]] auto default_zfs_layout(std::string_view zpool_name) noexcept
    -> std::vector<ZfsDatasetChoice>;

/// Create a zpool on @p partition populated with the CachyOS default
/// dataset layout, then export+reimport with altroot at @p mountpoint so
/// subsequent install steps see the datasets mounted under it.
[[nodiscard]] auto prepare_default_zpool(std::string_view partition,
    std::string_view zpool_name,
    std::string_view mountpoint) noexcept
    -> std::expected<void, std::string>;

/// Inputs to format a new LUKS-encrypted partition. All three string fields
/// must be non-empty; @ref extra_flags is optional.
struct LuksFormatRequest {
    /// Block device to format. Will be overwritten.
    std::string device;
    /// dm-crypt mapper name (no `/dev/mapper/` prefix).
    std::string mapper_name;
    /// Passphrase used both to format and to immediately reopen the device.
    std::string passphrase;
    /// Extra flags appended to `cryptsetup luksFormat`. Leave empty for
    /// CachyOS defaults.
    std::string extra_flags;
};

/// Inputs to open an already-encrypted LUKS partition.
struct LuksOpenRequest {
    /// Existing LUKS-formatted block device.
    std::string device;
    /// dm-crypt mapper name (no `/dev/mapper/` prefix).
    std::string mapper_name;
    /// Passphrase that unlocks the device.
    std::string passphrase;
};

/// Format @p req.device as LUKS1 with the given passphrase and extra flags,
/// then immediately open it.
[[nodiscard]] auto encrypt_partition(const LuksFormatRequest& req) noexcept
    -> std::expected<void, std::string>;

/// Open an existing LUKS1 partition.
[[nodiscard]] auto open_encrypted_partition(const LuksOpenRequest& req) noexcept
    -> std::expected<void, std::string>;

}  // namespace cachyos::installer::partition_planner
