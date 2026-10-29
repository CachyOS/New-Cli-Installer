#pragma once

#include "cachyos/types.hpp"

// import gucc
#include "gucc/bootloader.hpp"
#include "gucc/btrfs.hpp"
#include "gucc/partition.hpp"

#include <cinttypes>  // for uint8_t

#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for pair
#include <vector>       // for vector

namespace utils {

// re-export instlib
using cachyos::installer::AdditionalPartSelection;
using cachyos::installer::EspSelection;
using cachyos::installer::MountSelections;
using cachyos::installer::RootPartitionSelection;
using cachyos::installer::SwapSelection;

auto default_btrfs_subvolumes() noexcept -> std::vector<gucc::fs::BtrfsSubvolume>;
auto select_btrfs_subvolumes(std::string_view root_mountpoint) noexcept -> std::vector<gucc::fs::BtrfsSubvolume>;
[[nodiscard]] auto mount_existing_subvols(std::vector<gucc::fs::Partition>& partitions) noexcept -> bool;

// Creates swapfile or swap partition
[[nodiscard]] auto apply_swap_selection(const SwapSelection& swap, std::vector<gucc::fs::Partition>& partitions) noexcept -> bool;

// Handles creation of ESP/boot partition
[[nodiscard]] auto apply_esp_selection(const EspSelection& esp, std::vector<gucc::fs::Partition>& partitions, bool quiet = false) noexcept -> bool;

// Handles partitioning of the root
[[nodiscard]] auto apply_root_partition_actions(const RootPartitionSelection& selection, const std::vector<gucc::fs::BtrfsSubvolume>& btrfs_subvols, std::vector<gucc::fs::Partition>& partition_schema) noexcept -> bool;

// Mounts partitions
[[nodiscard]] auto apply_mount_selections(const MountSelections& selections) noexcept -> bool;

// Mount the "current" partition from config state using auto-detected defaults.
auto mount_partition_headless() noexcept -> bool;

// Setup ESP partition in headless mode (no TUI interaction).
auto make_esp_headless(std::vector<gucc::fs::Partition>& partitions, std::string_view part_name,
    gucc::bootloader::BootloaderType bootloader_type, bool reformat_part = true,
    std::string_view boot_part_mountpoint = {}) noexcept -> void;

/// Partition with the LUKS mapper/UUID of the last mount.
[[nodiscard]] auto attach_luks_info(gucc::fs::Partition partition) noexcept -> gucc::fs::Partition;

auto lvm_show_vg() noexcept -> std::vector<std::pair<std::string, std::string>>;

[[nodiscard]] bool zfs_auto_pres(const std::string_view& partition, const std::string_view& zfs_zpool_name) noexcept;
[[nodiscard]] bool zfs_create_zpool(const std::string_view& partition, const std::string_view& pool_name) noexcept;

void select_filesystem(std::string_view file_sys) noexcept;
auto get_available_mount_opts(std::string_view fstype) noexcept -> std::vector<std::string>;

/// Defaults for fstype, with specific device options.
[[nodiscard]] auto default_mount_opts(std::string_view device, std::string_view fstype) noexcept -> std::string;

auto mount_partition(std::string_view partition, std::string_view mountpoint, std::string_view mount_dev, std::string_view mount_opts) noexcept -> bool;
auto is_volume_removable() noexcept -> bool;

}  // namespace utils
