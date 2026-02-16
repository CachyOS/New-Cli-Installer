#ifndef DISK_HPP
#define DISK_HPP

#include "gucc/btrfs.hpp"
#include "gucc/partition.hpp"

#include <cinttypes>  // for uint8_t

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace utils {

// Structure to hold user selections for the root partition
struct RootPartitionSelection {
    std::string device;
    // Filesystem type
    std::string fstype;
    // Full mkfs command
    std::string mkfs_command;
    // Comma-separated mount options
    std::string mount_opts;
    // Flag indicating if formatting was chosen
    bool format_requested{};
};

// User selections for swap
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

// User selections for the EFI/boot partition
struct EspSelection {
    std::string device;
    std::string mountpoint;
    bool format_requested{};
};

// User selections for additional partitions
struct AdditionalPartSelection {
    std::string device;
    std::string mountpoint;
    std::string fstype;
    std::string mkfs_command;
    std::string mount_opts;
    bool format_requested{};
};

// Struct with all partition selections
struct MountSelections {
    RootPartitionSelection root;
    SwapSelection swap;
    EspSelection esp;
    std::vector<AdditionalPartSelection> additional;
    std::vector<gucc::fs::BtrfsSubvolume> btrfs_subvolumes;
};

auto default_btrfs_subvolumes() noexcept -> std::vector<gucc::fs::BtrfsSubvolume>;
auto select_btrfs_subvolumes(std::string_view root_mountpoint) noexcept -> std::vector<gucc::fs::BtrfsSubvolume>;
[[nodiscard]] auto mount_existing_subvols(std::vector<gucc::fs::Partition>& partitions) noexcept -> bool;

// Creates btrfs subvolumes
[[nodiscard]] auto apply_btrfs_subvolumes(const std::vector<gucc::fs::BtrfsSubvolume>& btrfs_subvols, const RootPartitionSelection& selection, std::string_view mountpoint_info, std::vector<gucc::fs::Partition>& partition_schema) noexcept -> bool;

// Creates swapfile or swap partition
[[nodiscard]] auto apply_swap_selection(const SwapSelection& swap, std::vector<gucc::fs::Partition>& partitions) noexcept -> bool;

// Handles creation of ESP/boot partition
[[nodiscard]] auto apply_esp_selection(const EspSelection& esp, std::vector<gucc::fs::Partition>& partitions) noexcept -> bool;

// Handles partitioning of the root
[[nodiscard]] auto apply_root_partition_actions(const RootPartitionSelection& selection, const std::vector<gucc::fs::BtrfsSubvolume>& btrfs_subvols, std::vector<gucc::fs::Partition>& partition_schema) noexcept -> bool;

// Mounts partitions
[[nodiscard]] auto apply_mount_selections(const MountSelections& selections) noexcept -> bool;

auto lvm_show_vg() noexcept -> std::vector<std::string>;

[[nodiscard]] bool zfs_auto_pres(const std::string_view& partition, const std::string_view& zfs_zpool_name) noexcept;
[[nodiscard]] bool zfs_create_zpool(const std::string_view& partition, const std::string_view& pool_name) noexcept;

void select_filesystem(std::string_view file_sys) noexcept;
auto get_available_mount_opts(std::string_view fstype) noexcept -> std::vector<std::string>;
auto mount_partition(std::string_view partition, std::string_view mountpoint, std::string_view mount_dev, std::string_view mount_opts) noexcept -> bool;
auto is_volume_removable() noexcept -> bool;

}  // namespace utils

#endif  // DISK_HPP
