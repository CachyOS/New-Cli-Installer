#ifndef ZFS_HPP
#define ZFS_HPP

#include <string>       // for string
#include <string_view>  // for string_view

namespace gucc::fs {

// Creates a zfs volume
void zfs_create_zvol(std::string_view zsize, std::string_view zpath) noexcept;

// Creates a zfs filesystem, the first parameter is the ZFS path and the second is the mount path
void zfs_create_dataset(std::string_view zpath, std::string_view zmount) noexcept;

void zfs_destroy_dataset(std::string_view zdataset) noexcept;

// returns a list of imported zpools
auto zfs_list_pools() noexcept -> std::string;

// returns a list of devices containing zfs members
auto zfs_list_devs() noexcept -> std::string;

auto zfs_list_datasets(std::string_view type = "none") noexcept -> std::string;

void zfs_set_property(std::string_view property, std::string_view dataset) noexcept;

}  // namespace gucc::fs

#endif  // ZFS_HPP
