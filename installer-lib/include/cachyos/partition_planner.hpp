#pragma once

#include "cachyos/types.hpp"

#include <cstdint>      // for uint64_t
#include <expected>     // for expected
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for pair
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

/// @brief An unallocated free region on a disk, in bytes.
///
/// Mirror of gucc::disk::FreeRegion so frontends don't pull in gucc headers.
struct FreeSpaceRegion {
    std::uint64_t start_bytes{};  ///< first byte of the gap
    std::uint64_t end_bytes{};    ///< last byte of the gap (inclusive)
    std::uint64_t size_bytes{};   ///< gap size in bytes
};

/// @brief List the unallocated free regions on a disk (non-destructive).
///
/// Powers the "install alongside" flow: the user picks a gap to install into
/// rather than wiping the disk or shrinking an existing partition.
[[nodiscard]] auto list_free_space(std::string_view device) noexcept
    -> std::vector<FreeSpaceRegion>;

/// @brief Create a root partition in a free region and return its device path.
///
/// Non-destructive to existing partitions. The returned path can be used as a
/// Replace-style root device in a PartitionPlan (format it, mount at "/").
[[nodiscard]] auto prepare_alongside_partition(std::string_view device,
    std::uint64_t start_bytes, std::uint64_t end_bytes) noexcept
    -> std::expected<std::string, std::string>;

/// @brief An existing partition on a disk (mirror of gucc::disk::PartitionLayout,
/// with the resolved device path the frontend operates on).
struct PartitionEntry {
    std::uint32_t number{};       ///< 1-based partition number
    std::uint64_t start_bytes{};  ///< first byte
    std::uint64_t end_bytes{};    ///< last byte (inclusive)
    std::uint64_t size_bytes{};   ///< size in bytes
    std::string device;           ///< resolved path (e.g. /dev/sda2 or /dev/nvme0n1p2)
    std::string fstype;           ///< filesystem ("ext4", "ntfs", "" if none)
    std::string name;             ///< GPT partition name
    std::string flags;            ///< comma-separated flags ("boot, esp")
    std::string part_type;        ///< human partition type ("EFI System", "Linux filesystem")
    std::string label;            ///< filesystem label
    std::string uuid;             ///< filesystem UUID
    std::uint64_t used_bytes{};   ///< bytes in use on the filesystem (0 if unknown)
};

/// @brief Lists the existing partitions on a disk (powers the Manual editor's
/// partition-bars view and the Alongside shrink slider). Non-destructive.
[[nodiscard]] auto list_partitions(std::string_view device) noexcept
    -> std::vector<PartitionEntry>;

/// @brief The shrink range for a partition: {minimum, current} bytes. The minimum
/// is the space the filesystem is actually using.
[[nodiscard]] auto shrinkable_bounds(std::string_view device, std::uint32_t number,
    std::string_view fstype, std::uint64_t current_size_bytes) noexcept
    -> std::expected<std::pair<std::uint64_t, std::uint64_t>, std::string>;

/// @brief One pending partition-table edit.
struct PartitionOp {
    enum class Kind : std::uint8_t {
        Create,    ///< new partition in [start,end], optionally formatted/flagged
        Delete,    ///< remove partition `number`
        Resize,    ///< shrink/grow partition `number` (fs → size_bytes, end → end_bytes)
        Format,    ///< (re)format partition `number` as fstype (+ label)
        SetFlag,   ///< set/clear `flag` on partition `number`
        SetLabel,  ///< set fs label on partition `number`
        NewTable,  ///< write a fresh `table_type` partition table (wipes the disk)
    };
    Kind kind{};
    std::uint32_t number{};       ///< target partition (Delete/Resize/Format/SetFlag/SetLabel)
    std::uint64_t start_bytes{};  ///< Create
    std::uint64_t end_bytes{};    ///< Create / Resize (new partition end)
    std::uint64_t size_bytes{};   ///< Resize (new filesystem size)
    std::string fstype;           ///< Create/Format/Resize
    std::string label;            ///< Create/Format/SetLabel
    std::string flags;            ///< Create (comma list) / SetFlag (single flag)
    bool flag_state{true};        ///< SetFlag on/off
    std::string table_type;       ///< NewTable ("gpt"/"msdos")
};

/// @brief Applies pending partition operations in order via the gucc partition
/// layer, returning the device path of the last partition created (the typical
/// install target for Alongside/Manual), or empty when none was created.
///
/// @warning Destructive. Ops referencing a `number` use the table as it stands
/// when the op runs; the frontend should avoid interleaving deletes with
/// number-based edits.
[[nodiscard]] auto apply_partition_operations(std::string_view device,
    const std::vector<PartitionOp>& ops) noexcept
    -> std::expected<std::string, std::string>;

/// @brief LUKS header version on disk.
enum class LuksVersion : std::uint8_t {
    Luks1,  ///< old header, only when the bootloader can't read LUKS2
    Luks2,  ///< the normal choice
};

/// @brief One mount the frontend wants once the partition ops have been applied.
///
/// `created` rows refer to the partition produced by the (single) Create op in
/// the staged op list — their device is resolved from apply_partition_operations'
/// return value, so `device` is left empty. Existing rows carry their path.
///
/// When `luks` is set the (created) partition is luksFormat'd and opened as
/// `luks_mapper_name` before it is formatted/mounted, so the matching Create op
/// must leave its fstype empty (the raw partition is not pre-formatted) and this
/// mount's `format_requested` must be true (the mapper gets the filesystem).
struct StagedMount {
    bool created{};                ///< true → device comes from the apply result
    std::string device;            ///< existing partition path (when !created)
    std::string mountpoint;        ///< "/", "/boot/efi", "/home", …
    std::string fstype;            ///< filesystem for this mount
    bool format_requested{};       ///< (re)format before mounting
    std::string mount_opts;        ///< extra mount options
    bool is_esp{};                 ///< this mount is the EFI system partition
    bool luks{};                   ///< encrypt with LUKS before format/mount
    std::string luks_passphrase;   ///< passphrase (when luks)
    std::string luks_mapper_name;  ///< dm-crypt mapper name, no /dev/mapper/ prefix
    LuksVersion luks_version{LuksVersion::Luks2};  ///< on-disk header version
};

/// @brief Apply pending partition ops, then lower the resulting layout into the
/// MountSelections the orchestrator consumes — the destructive counterpart of
/// finalize_plan, meant to run at install time (as root) off the UI thread.
///
/// Runs @p ops in order (the last Create is the install target), resolves the
/// `created` StagedMount rows to that partition, builds a PartitionPlan (default
/// btrfs subvolumes when root is btrfs) and finalizes it.
///
/// @warning Destructive (drives parted + fs tools). Requires root.
[[nodiscard]] auto apply_partition_plan(std::string_view device,
    const std::vector<PartitionOp>& ops,
    const std::vector<StagedMount>& mounts) noexcept
    -> std::expected<MountSelections, std::string>;

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
