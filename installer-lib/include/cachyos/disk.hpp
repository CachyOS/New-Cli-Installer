#pragma once

#include "cachyos/types.hpp"

// import gucc
#include "gucc/btrfs.hpp"
#include "gucc/partition.hpp"
#include "gucc/zfs_types.hpp"

#include <cstdint>      // for int32_t
#include <expected>     // for expected
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

// forward-declare
namespace gucc::utils {
class SubProcess;
}

namespace cachyos::installer {

/// Result of mounting a single partition with LUKS/LVM detection.
struct MountPartitionResult {
    std::int32_t is_luks{};
    std::int32_t is_lvm{};
    std::string luks_name;
    std::string luks_dev;
    std::string luks_uuid;
    std::string fstype;
    std::string uuid;
};

/// Result of applying swap selection.
struct SwapApplicationResult {
    std::string swap_device;
    std::optional<gucc::fs::Partition> swap_partition;
};

/// Result of applying root partition actions.
struct RootPartitionResult {
    std::vector<gucc::fs::Partition> partitions;
    MountPartitionResult mount_info;
};

/// Result of applying mount selections.
struct MountApplicationResult {
    std::vector<gucc::fs::Partition> partitions;
    std::string swap_device;
    std::int32_t lvm_sep_boot{0};
};

/// Returns the default set of btrfs subvolumes used by CachyOS.
[[nodiscard]] auto default_btrfs_subvolumes() noexcept -> std::vector<gucc::fs::BtrfsSubvolume>;

/// Returns the default set of ZFS datasets used by CachyOS for a given pool.
[[nodiscard]] auto default_zfs_datasets(std::string_view zpool_name) noexcept
    -> std::vector<gucc::fs::ZfsDataset>;

/// Returns available mount options for a given filesystem type.
[[nodiscard]] auto get_available_mount_opts(std::string_view fstype) noexcept -> std::vector<std::string>;

/// Returns true if the installation target volume is removable (USB, etc.).
[[nodiscard]] auto is_volume_removable(std::string_view mountpoint) noexcept -> bool;

/// EFI system partition layout (mountpoint + size) for a bootloader
struct EspLayout {
    std::string mountpoint;  ///< "/boot" or "/boot/efi"
    std::string size;        ///< gucc size string, e.g. "512MiB", "2GiB", "4GiB"
};

/// Returns the ESP mountpoint+size the given bootloader expects. Mountpoint comes
/// from the existing per-bootloader mapping.
[[nodiscard]] auto bootloader_esp_layout(gucc::bootloader::BootloaderType bootloader,
    std::string_view system_mode) noexcept -> EspLayout;

/// Performs automatic partitioning of the device.
[[nodiscard]] auto auto_partition(std::string_view device, std::string_view system_mode,
    gucc::bootloader::BootloaderType bootloader, const ExecutionCallbacks& callbacks) noexcept
    -> std::expected<std::vector<gucc::fs::Partition>, std::string>;

/// Securely wipes a device.
[[nodiscard]] auto secure_wipe(std::string_view device, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string>;

/// Automated ZFS setup: creates a new zpool with default datasets. When
/// @p passphrase is set the pool is created with ZFS native encryption.
[[nodiscard]] auto zfs_auto_pres(std::string_view partition,
    std::string_view zpool_name, std::string_view mountpoint,
    std::optional<std::string> passphrase = std::nullopt) noexcept
    -> std::expected<gucc::fs::ZfsSetupConfig, std::string>;

/// Erases @p device, lays down an ESP (UEFI) + a pool partition for the chosen
/// bootloader, creates the default zpool (optionally encrypted) on it, imports it
/// with altroot at @p mountpoint, and mounts the ESP. Returns the zpool name.
/// The caller marks the install prepartitioned and records the pool name so the
/// later steps (fstab/cleanup) see the datasets already mounted.
[[nodiscard]] auto apply_zfs_root_layout(std::string_view device, std::string_view zpool_name,
    std::optional<std::string> passphrase, std::string_view system_mode,
    gucc::bootloader::BootloaderType bootloader, std::string_view mountpoint,
    const ExecutionCallbacks& callbacks) noexcept
    -> std::expected<std::string, std::string>;

/// Creates a new zpool on an existing partition.
[[nodiscard]] auto zfs_create_zpool(std::string_view partition,
    std::string_view pool_name, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string>;

/// Applies the user's partition/mount selections: formats, mounts, and builds partition schema.
[[nodiscard]] auto apply_mount_selections(const MountSelections& selections,
    std::string_view mountpoint) noexcept
    -> std::expected<MountApplicationResult, std::string>;

/// Formats (when requested) and mounts the given additional partitions, appending the
/// resulting partition structs to `partitions`. Returns the lvm_sep_boot indicator
/// (0 = none, 1 = separate /boot, 2 = separate lvm /boot).
[[nodiscard]] auto apply_additional_partitions(const std::vector<AdditionalPartSelection>& additional,
    std::string_view mountpoint, std::vector<gucc::fs::Partition>& partitions) noexcept
    -> std::expected<std::int32_t, std::string>;

/// Creates btrfs subvolumes according to the selection.
[[nodiscard]] auto apply_btrfs_subvolumes(const std::vector<gucc::fs::BtrfsSubvolume>& subvols,
    const RootPartitionSelection& selection,
    std::string_view mountpoint,
    std::vector<gucc::fs::Partition>& partitions) noexcept
    -> std::expected<void, std::string>;

/// Sets up the EFI System Partition.
[[nodiscard]] auto setup_esp_partition(std::string_view device, std::string_view mountpoint,
    std::string_view root_mountpoint, bool format_requested) noexcept
    -> std::expected<gucc::fs::Partition, std::string>;

/// Unmounts all partitions under the given mountpoint.
[[nodiscard]] auto umount_partitions(std::string_view mountpoint,
    const std::vector<std::string>& zfs_pools,
    std::string_view swap_device) noexcept
    -> std::expected<void, std::string>;

/// Generates fstab for the installed system.
[[nodiscard]] auto generate_fstab(std::string_view mountpoint) noexcept
    -> std::expected<void, std::string>;

/// Mounts a partition, creates directories, and queries LUKS/LVM state.
[[nodiscard]] auto mount_partition(std::string_view partition, std::string_view mountpoint,
    std::string_view mount_dev, std::string_view mount_opts) noexcept
    -> std::expected<MountPartitionResult, std::string>;

/// Creates swap (file or partition) and returns the device path and optional partition struct.
[[nodiscard]] auto apply_swap(const SwapSelection& swap,
    std::string_view mountpoint) noexcept
    -> std::expected<SwapApplicationResult, std::string>;

/// Formats, mounts root partition, builds partition struct, and applies btrfs subvolumes.
[[nodiscard]] auto apply_root_partition(const RootPartitionSelection& selection,
    const std::vector<gucc::fs::BtrfsSubvolume>& btrfs_subvols,
    std::string_view mountpoint) noexcept
    -> std::expected<RootPartitionResult, std::string>;

/// Loads kernel module for specific filesystem if needed.
void load_filesystem_module(std::string_view fstype) noexcept;

/// Lists mounted devices under the given mountpoint as a joined string.
[[nodiscard]] auto list_mounted_devices(std::string_view mountpoint) noexcept -> std::string;

/// Lists devices containing LUKS crypto as a newline-separated string.
[[nodiscard]] auto list_containing_crypt() noexcept -> std::string;

/// Lists devices NOT containing LUKS crypto as a newline-separated string.
[[nodiscard]] auto list_non_crypt() noexcept -> std::string;

}  // namespace cachyos::installer
