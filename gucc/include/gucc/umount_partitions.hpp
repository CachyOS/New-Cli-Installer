#ifndef UMOUNT_PARTITIONS_HPP
#define UMOUNT_PARTITIONS_HPP

#include "gucc/export.hpp"

#include "gucc/error.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

GUCC_EXPORT_BEGIN

namespace gucc::umount {

// Umount partitions
auto umount_partitions(std::string_view root_mountpoint, const std::vector<std::string>& zfs_poolnames) noexcept -> Result<void>;

}  // namespace gucc::umount

GUCC_EXPORT_END

#endif  // UMOUNT_PARTITIONS_HPP
