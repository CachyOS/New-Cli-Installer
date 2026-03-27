#ifndef CACHYOS_INSTALLER_DISK_HPP
#define CACHYOS_INSTALLER_DISK_HPP

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

/// Re-export disk selection types from the TUI layer for use by the library.
/// TODO(vnepogodin): refactor later.
struct RootPartitionSelection {
    std::string device;
    std::string fstype;
    std::string mkfs_command;
    std::string mount_opts;
    bool format_requested{};
};

struct SwapSelection {
    enum class Type : std::uint8_t {
        None,
        Swapfile,
        Partition
    };

    Type type{Type::None};
    std::string device;
    std::string swapfile_size;
    bool needs_mkswap{};
};

struct EspSelection {
    std::string device;
    std::string mountpoint;
    bool format_requested{};
};

struct AdditionalPartSelection {
    std::string device;
    std::string mountpoint;
    std::string fstype;
    std::string mkfs_command;
    std::string mount_opts;
    bool format_requested{};
};

struct MountSelections {
    RootPartitionSelection root;
    SwapSelection swap;
    EspSelection esp;
    std::vector<AdditionalPartSelection> additional;
    std::vector<gucc::fs::BtrfsSubvolume> btrfs_subvolumes;
};

/// Result of mounting a single partition with LUKS/LVM detection.
struct MountPartitionResult {
    std::int32_t is_luks{};
    std::int32_t is_lvm{};
    std::string luks_name;
    std::string luks_dev;
    std::string luks_uuid;
    std::string fstype;  // detected filesystem type at mountpoint
    std::string uuid;    // device UUID
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

/// Returns available mount options for a given filesystem type.
[[nodiscard]] auto get_available_mount_opts(std::string_view fstype) noexcept -> std::vector<std::string>;

/// Returns true if the installation target volume is removable (USB, etc.).
[[nodiscard]] auto is_volume_removable(std::string_view mountpoint) noexcept -> bool;

/// Performs automatic partitioning of the device.
[[nodiscard]] auto auto_partition(std::string_view device, std::string_view system_mode,
    gucc::bootloader::BootloaderType bootloader, const ExecutionCallbacks& callbacks) noexcept
    -> std::expected<std::vector<gucc::fs::Partition>, std::string>;

/// Securely wipes a device.
[[nodiscard]] auto secure_wipe(std::string_view device, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string>;

/// Automated ZFS setup: creates a new zpool with default datasets.
[[nodiscard]] auto zfs_auto_pres(std::string_view partition,
    std::string_view zpool_name, std::string_view mountpoint) noexcept
    -> std::expected<gucc::fs::ZfsSetupConfig, std::string>;

/// Creates a new zpool on an existing partition.
[[nodiscard]] auto zfs_create_zpool(std::string_view partition,
    std::string_view pool_name, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string>;

/// Applies the user's partition/mount selections: formats, mounts, and builds partition schema.
[[nodiscard]] auto apply_mount_selections(const MountSelections& selections,
    std::string_view mountpoint) noexcept
    -> std::expected<MountApplicationResult, std::string>;

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

#endif  // CACHYOS_INSTALLER_DISK_HPP
