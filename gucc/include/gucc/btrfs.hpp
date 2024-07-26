#ifndef BTRFS_HPP
#define BTRFS_HPP

#include "gucc/partition.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::fs {

struct BtrfsSubvolume final {
    std::string subvolume;
    std::string mountpoint;
};

// Creates btrfs subvolume
auto btrfs_create_subvol(std::string_view subvolume, std::string_view root_mountpoint) noexcept -> bool;

// Creates btrfs subvolumes and mounts them
auto btrfs_create_subvols(const std::vector<BtrfsSubvolume>& subvols, std::string_view device, std::string_view root_mountpoint, std::string_view mount_opts) noexcept -> bool;

// Mounts btrfs subvolumes
auto btrfs_mount_subvols(const std::vector<BtrfsSubvolume>& subvols, std::string_view device, std::string_view root_mountpoint, std::string_view mount_opts) noexcept -> bool;

// Appends btrfs subvolumes into Partition scheme
// with sorting scheme by device field
auto btrfs_append_subvolumes(std::vector<Partition>& partitions, const std::vector<BtrfsSubvolume>& subvols) noexcept -> bool;

}  // namespace gucc::fs

#endif  // BTRFS_HPP
