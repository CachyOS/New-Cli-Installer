#ifndef UMOUNT_PARTITIONS_HPP
#define UMOUNT_PARTITIONS_HPP

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::umount {

// Umount partitions
auto umount_partitions(std::string_view root_mountpoint, const std::vector<std::string>& zfs_poolnames) noexcept -> bool;

}  // namespace gucc::umount

#endif  // UMOUNT_PARTITIONS_HPP
