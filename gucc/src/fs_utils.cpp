#include "gucc/fs_utils.hpp"
#include "gucc/block_devices.hpp"
#include "gucc/io_utils.hpp"

#include <utility>  // for move

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
    return gucc::disk::query_block_device(device)
        .transform([](gucc::disk::BlockDevice dev) { return DeviceInfo{.fstype = std::move(dev.fstype), .uuid = std::move(dev.uuid)}; })
        .value_or(DeviceInfo{});
}

auto get_device_uuid(std::string_view device) noexcept -> std::string {
    return get_device_info(device).uuid;
}

auto get_device_fstype(std::string_view device) noexcept -> std::string {
    return get_device_info(device).fstype;
}

}  // namespace gucc::fs::utils
