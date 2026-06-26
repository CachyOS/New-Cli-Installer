#pragma once

#include "cachyos/types.hpp"

#include <cstdint>      // for uint64_t
#include <expected>     // for expected
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

/// @file partition_planner.hpp
/// @brief Headless, frontend-agnostic disk planning.
///
/// A frontend does all of its partitioning
/// through the functions here. Call discover() to see what's on the system.
namespace cachyos::installer::partition_planner {

/// @brief One block device or partition shown to the user.
///
/// A flat lsblk-style entry.
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

/// @brief A btrfs subvolume on a currently-mounted btrfs filesystem.
struct ExistingBtrfsSubvolume {
    std::string device;     ///< block device behind the filesystem
    std::string subvolume;  ///< subvolume path inside it
};

/// @brief A ZFS pool that's imported on the live system right now.
struct ExistingZfsPool {
    std::string name;  ///< pool name from zpool
};

/// @brief An LVM volume group active on the live system.
struct ExistingLvmGroup {
    std::string name;  ///< volume group name
    std::string size;  ///< group size, the way LVM prints it
};

/// @brief One row of a btrfs subvolume layout.
///
/// Used for the CachyOS layout, and also for one we suggest over an
/// existing filesystem the user wants to keep.
struct BtrfsSubvolumeChoice {
    std::string subvolume;   ///< subvolume path, e.g. `@` or `@home`
    std::string mountpoint;  ///< where it gets mounted in the target
};

/// @brief Everything we found out about the disks.
///
/// Whatever discover() found.
struct DiskInventory {
    std::vector<DeviceEntry> block_devices;
    std::vector<ExistingBtrfsSubvolume> btrfs_subvolumes;
    std::vector<ExistingZfsPool> zfs_pools;
    std::vector<ExistingLvmGroup> lvm_groups;
};

/// @brief Look at the live system and report the disk state.
///
/// Usually the first thing a frontend calls. The DiskInventory is a snapshot, so
/// run it again after the user changes anything.
[[nodiscard]] auto discover() noexcept -> DiskInventory;

/// @brief The default CachyOS btrfs subvolume layout.
[[nodiscard]] auto default_btrfs_layout() noexcept -> std::vector<BtrfsSubvolumeChoice>;

/// @brief Guess a mountpoint for a subvolume path.
///
/// Turns a known subvolume name into its usual mountpoint (`@home` -> `/home`).
/// Empty string if we don't have a guess for it.
[[nodiscard]] auto suggest_mountpoint_for_subvolume(std::string_view subvol_path) noexcept
    -> std::string;

/// @brief List the subvolumes on a btrfs partition that isn't mounted.
///
/// Mounts @p device read-only, reads its subvolumes, unmounts again, so the
/// system is left as it was. Lets the user pick from an existing filesystem
/// before we commit to it.
[[nodiscard]] auto inspect_existing_btrfs(std::string_view device) noexcept
    -> std::expected<std::vector<std::string>, std::string>;

/// @brief One row of a ZFS dataset layout.
struct ZfsDatasetChoice {
    std::string dataset;  ///< dataset path
    /// mountpoint, or "none" for container datasets that shouldn't be mounted
    std::string mountpoint;
};

/// @brief The default CachyOS dataset layout for a pool.
[[nodiscard]] auto default_zfs_layout(std::string_view zpool_name) noexcept
    -> std::vector<ZfsDatasetChoice>;

/// @brief Create the default CachyOS zpool on a partition and set it up.
///
/// Creates a pool called @p zpool_name on @p partition, fills it with the
/// default_zfs_layout datasets, then exports and reimports it with altroot at
/// @p mountpoint so the later install steps see the datasets mounted under there.
[[nodiscard]] auto prepare_default_zpool(std::string_view partition,
    std::string_view zpool_name,
    std::string_view mountpoint) noexcept
    -> std::expected<void, std::string>;

/// @brief LUKS header version on disk.
enum class LuksVersion : std::uint8_t {
    Luks1,  ///< old header, only when the bootloader can't read LUKS2
    Luks2,  ///< the normal choice
};

/// @brief What you need to format a new LUKS partition.
///
/// device, mapper_name and passphrase all have to be non-empty. extra_flags is
/// optional.
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
    /// On-disk LUKS header version.
    LuksVersion version{LuksVersion::Luks2};
};

/// @brief What you need to open an existing LUKS partition.
struct LuksOpenRequest {
    /// Existing LUKS-formatted block device.
    std::string device;
    /// dm-crypt mapper name (no `/dev/mapper/` prefix).
    std::string mapper_name;
    /// Passphrase that unlocks the device.
    std::string passphrase;
    /// On-disk LUKS header version of the existing device.
    LuksVersion version{LuksVersion::Luks2};
};

/// @brief Format a partition as LUKS and open it straight away.
///
/// Formats req.device as LUKS (version from LuksFormatRequest::version) and opens
/// it as req.mapper_name, so the rest of the plan can treat /dev/mapper/<name>
/// like a normal block device.
/// @warning wipes everything on req.device.
[[nodiscard]] auto encrypt_partition(const LuksFormatRequest& req) noexcept
    -> std::expected<void, std::string>;

/// @brief Open an existing LUKS partition under its mapper name.
///
/// Version comes from LuksOpenRequest::version.
[[nodiscard]] auto open_encrypted_partition(const LuksOpenRequest& req) noexcept
    -> std::expected<void, std::string>;

/// @brief All the layout choices a frontend wizard has collected.
///
/// Hand it to finalize_plan to get the MountSelections that the orchestrator's
/// Partition step reads through InstallContext::mount_selections.
///
/// @note For an encrypted root, set root.device to the opened mapper path
/// (/dev/mapper/cryptroot or similar), not the raw partition.
struct PartitionPlan {
    RootPartitionSelection root;                         ///< the root partition
    SwapSelection swap;                                  ///< swap: partition, file or none
    EspSelection esp;                                    ///< the EFI system partition
    std::vector<AdditionalPartSelection> additional;     ///< other partitions to mount
    std::vector<BtrfsSubvolumeChoice> btrfs_subvolumes;  ///< subvolume layout for a btrfs root
};

/// @brief Turn a PartitionPlan into the MountSelections the installer uses.
///
/// Last step of planning. Checks the choices and lowers them into the form the
/// orchestrator wants.
[[nodiscard]] auto finalize_plan(PartitionPlan plan) noexcept
    -> std::expected<MountSelections, std::string>;

}  // namespace cachyos::installer::partition_planner
