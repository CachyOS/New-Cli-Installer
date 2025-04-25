#ifndef DISK_HPP
#define DISK_HPP

#include "gucc/partition.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace utils {

void btrfs_create_subvols(std::vector<gucc::fs::Partition>& partitions, const std::string_view& mode, bool ignore_note = false) noexcept;
void mount_existing_subvols(std::vector<gucc::fs::Partition>& partitions) noexcept;
auto lvm_show_vg() noexcept -> std::vector<std::string>;

// ZFS filesystem
[[nodiscard]] bool zfs_auto_pres(const std::string_view& partition, const std::string_view& zfs_zpool_name) noexcept;
[[nodiscard]] bool zfs_create_zpool(const std::string_view& partition, const std::string_view& pool_name) noexcept;

// Other filesystems
void select_filesystem(const std::string_view& file_sys) noexcept;

// Mounts and queries device setup
auto mount_partition(std::string_view partition, std::string_view mountpoint, std::string_view mount_dev, std::string_view mount_opts) noexcept -> bool;

}  // namespace utils

#endif  // DISK_HPP
