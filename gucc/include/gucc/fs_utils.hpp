#ifndef FS_UTILS_HPP
#define FS_UTILS_HPP

#include <string>       // for string
#include <string_view>  // for string_view

namespace gucc::fs::utils {

// Get FSTYPE of mountpoint
auto get_mountpoint_fs(std::string_view mountpoint) noexcept -> std::string;

// Get SOURCE of mountpoint
auto get_mountpoint_source(std::string_view mountpoint) noexcept -> std::string;

// Get UUID of device/partition
auto get_device_uuid(std::string_view device) noexcept -> std::string;

}  // namespace gucc::fs::utils

#endif  // FS_UTILS_HPP
