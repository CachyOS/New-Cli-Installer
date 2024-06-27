#ifndef BTRFS_HPP
#define BTRFS_HPP

#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::fs {

struct BtrfsSubvolume final {
    std::string_view subvolume;
    std::string_view mountpoint;
};

// Creates btrfs subvolume
auto btrfs_create_subvol(std::string_view subvolume, std::string_view root_mountpoint) noexcept -> bool;

// Creates btrfs subvolumes and mounts them
auto btrfs_create_subvols(const std::vector<BtrfsSubvolume>& subvols, std::string_view device, std::string_view root_mountpoint, std::string_view mount_opts) noexcept -> bool;

}  // namespace gucc::fs

#endif  // BTRFS_HPP
