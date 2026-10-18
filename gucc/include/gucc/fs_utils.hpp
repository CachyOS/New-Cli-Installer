#pragma once

#include <string>       // for string
#include <string_view>  // for string_view

namespace gucc::fs::utils {

// Get FSTYPE of mountpoint
auto get_mountpoint_fs(std::string_view mountpoint) noexcept -> std::string;

// Get SOURCE of mountpoint
auto get_mountpoint_source(std::string_view mountpoint) noexcept -> std::string;

struct DeviceInfo final {
    std::string fstype;
    std::string uuid;
};

// Get UUID and FSTYPE of device/partition
auto get_device_info(std::string_view device) noexcept -> DeviceInfo;

// Get UUID of device/partition
auto get_device_uuid(std::string_view device) noexcept -> std::string;

// Get FSTYPE of a device/partition
auto get_device_fstype(std::string_view device) noexcept -> std::string;

}  // namespace gucc::fs::utils
