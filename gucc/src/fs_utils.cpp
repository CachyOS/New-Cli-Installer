#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

namespace gucc::fs::utils {

auto get_mountpoint_fs(std::string_view mountpoint) noexcept -> std::string {
    return gucc::utils::exec(fmt::format(FMT_COMPILE("findmnt -ln -o FSTYPE \"{}\""), mountpoint));
}

auto get_mountpoint_source(std::string_view mountpoint) noexcept -> std::string {
    return gucc::utils::exec(fmt::format(FMT_COMPILE("findmnt -ln -o SOURCE \"{}\""), mountpoint));
}

auto get_device_info(std::string_view device) noexcept -> DeviceInfo {
    // e.g. FSTYPE="vfat" UUID="FB54-6E42"
    const auto& output = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -dnPo FSTYPE,UUID '{}'"), device));
    const auto field   = [&output](std::string_view key) {
        return std::string{gucc::utils::extract_after(output, key, '"').value_or("")};
    };
    return DeviceInfo{.fstype = field("FSTYPE=\""), .uuid = field("UUID=\"")};
}

auto get_device_uuid(std::string_view device) noexcept -> std::string {
    return get_device_info(device).uuid;
}

auto get_device_fstype(std::string_view device) noexcept -> std::string {
    return get_device_info(device).fstype;
}

}  // namespace gucc::fs::utils
