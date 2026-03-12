#ifndef CACHYOS_INSTALLER_DISK_HPP
#define CACHYOS_INSTALLER_DISK_HPP

#include "cachyos/types.hpp"

#include "gucc/btrfs.hpp"
#include "gucc/partition.hpp"

#include <expected>     // for expected
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

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

/// Returns the default set of btrfs subvolumes used by CachyOS.
[[nodiscard]] auto default_btrfs_subvolumes() noexcept -> std::vector<gucc::fs::BtrfsSubvolume>;

/// Returns available mount options for a given filesystem type.
[[nodiscard]] auto get_available_mount_opts(std::string_view fstype) noexcept -> std::vector<std::string>;

/// Returns true if the installation target volume is removable (USB, etc.).
[[nodiscard]] auto is_volume_removable(std::string_view device) noexcept -> bool;

/// Performs automatic partitioning of the device.
[[nodiscard]] auto auto_partition(std::string_view device, std::string_view system_mode,
    gucc::bootloader::BootloaderType bootloader, const ExecutionCallbacks& callbacks) noexcept
    -> std::expected<std::vector<gucc::fs::Partition>, std::string>;

/// Applies the user's partition/mount selections and returns the resulting partition schema.
[[nodiscard]] auto apply_mount_selections(const MountSelections& selections,
    std::string_view mountpoint) noexcept
    -> std::expected<std::vector<gucc::fs::Partition>, std::string>;

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

}  // namespace cachyos::installer

#endif  // CACHYOS_INSTALLER_DISK_HPP
