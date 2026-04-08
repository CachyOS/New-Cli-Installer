#pragma once

#include "cachyos/export.hpp"

#include "cachyos/types.hpp"

// import gucc
#include "gucc/luks.hpp"

#include <cstdint>      // for uint64_t
#include <expected>     // for expected
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for forward, forward_like, pair
#include <vector>       // for vector

/// @file partition_planner.hpp
/// @brief Headless, frontend-agnostic disk planning.
///
/// A frontend does all of its partitioning
/// through the functions here. Call discover() to see what's on the system.
CACHYOS_INSTALLER_EXPORT_BEGIN

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

/// @brief Convert between subvolume-row types.
///
/// Moves the strings out when @p subvols is an rvalue.
template <class Out, std::ranges::input_range R>
    requires requires(std::ranges::range_reference_t<R> subvol) {
        Out{.subvolume = subvol.subvolume, .mountpoint = subvol.mountpoint};
    }
[[nodiscard]] auto convert_subvolumes(R&& subvols) -> std::vector<Out> {
    return std::forward<R>(subvols)
        | std::ranges::views::transform([](auto&& subvol) {
              return Out{.subvolume = std::forward_like<R>(subvol.subvolume), .mountpoint = std::forward_like<R>(subvol.mountpoint)};
          })
        | std::ranges::to<std::vector<Out>>();
}

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
using LuksVersion = gucc::crypto::LuksVersion;

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

/// @brief An unallocated gap on a disk.
struct FreeSpaceRegion {
    std::uint64_t start_bytes{};  ///< first byte
    std::uint64_t end_bytes{};    ///< last byte, inclusive
    std::uint64_t size_bytes{};
};

/// @brief Free gaps on @p device, derived from lsblk so it works without root.
[[nodiscard]] auto list_free_space(std::string_view device) noexcept -> std::vector<FreeSpaceRegion>;

/// @brief An existing partition on a disk.
struct PartitionEntry {
    std::uint32_t number{};       ///< 1-based partition number
    std::uint64_t start_bytes{};  ///< first byte
    std::uint64_t end_bytes{};    ///< last byte, inclusive
    std::uint64_t size_bytes{};
    std::string device;          ///< partition node, e.g. /dev/nvme0n1p2
    std::string fstype;          ///< empty if none
    std::string flags;           ///< parted-style, e.g. "boot, esp"
    std::string part_type;       ///< e.g. "EFI System"
    std::string label;           ///< filesystem label
    std::uint64_t used_bytes{};  ///< 0 unless mounted
};

/// @brief Partitions of @p device ordered by start.
[[nodiscard]] auto list_partitions(std::string_view device) noexcept -> std::vector<PartitionEntry>;

/// @brief {minimum, current} size partition @p number can be shrunk between.
[[nodiscard]] auto shrinkable_bounds(std::string_view device, std::uint32_t number,
    std::string_view fstype, std::uint64_t current_size_bytes) noexcept
    -> std::expected<std::pair<std::uint64_t, std::uint64_t>, std::string>;

/// @brief One pending partition table edit.
struct PartitionOp {
    enum class Kind : std::uint8_t {
        Create,    ///< new partition in [start_bytes, end_bytes], optionally formatted and flagged
        Delete,    ///< remove `number`
        Resize,    ///< shrink `number`: filesystem to size_bytes, partition end to end_bytes
        Format,    ///< format `number` as fstype
        SetFlag,   ///< set `flags` (a single flag) on `number` to flag_state
        SetLabel,  ///< set filesystem label of `number`
        NewTable,  ///< write an empty `table_type` table
    };
    Kind kind{};
    std::uint32_t number{};
    std::uint64_t start_bytes{};
    std::uint64_t end_bytes{};
    std::uint64_t size_bytes{};
    std::string fstype;
    std::string label;
    std::string flags;  ///< comma list for Create
    bool flag_state{true};
    std::string table_type;  ///< "gpt" or "msdos"
};

/// @brief A mount wanted once the partition ops ran.
///
/// A `created` row is the partition made by the last Create op. With `luks`
/// the partition is encrypted and opened as `luks_mapper_name` first, so its
/// Create op must leave fstype empty and the row must request a format.
struct StagedMount {
    bool created{};          ///< device comes from the last Create op
    std::string device;      ///< existing partition, when !created
    std::string mountpoint;  ///< "/", "/boot/efi", "/home", ...
    std::string fstype;
    bool format_requested{};
    std::string mount_opts;
    bool is_esp{};
    bool luks{};
    std::string luks_passphrase;
    std::string luks_mapper_name;  ///< without /dev/mapper/
    LuksVersion luks_version{LuksVersion::Luks2};
};

/// @brief Run @p ops in order on @p device, then finalize @p mounts into
/// the selections partition_strategy::ApplyLayout takes.
///
/// Ops that address a `number` see the table as it is when they run.
/// @warning destructive, needs root.
[[nodiscard]] auto apply_partition_plan(std::string_view device,
    const std::vector<PartitionOp>& ops,
    const std::vector<StagedMount>& mounts) noexcept
    -> std::expected<MountSelections, std::string>;

/// @brief All the layout choices a frontend wizard has collected.
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

CACHYOS_INSTALLER_EXPORT_END
