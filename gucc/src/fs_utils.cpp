#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

namespace gucc::fs::utils {

auto get_mountpoint_fs(std::string_view mountpoint) noexcept -> std::string {
    return gucc::utils::exec(fmt::format(FMT_COMPILE("findmnt -ln -o FSTYPE \"{}\""), mountpoint));
}

auto get_mountpoint_source(std::string_view mountpoint) noexcept -> std::string {
    return gucc::utils::exec(fmt::format(FMT_COMPILE("findmnt -ln -o SOURCE \"{}\""), mountpoint));
}

auto get_device_uuid(std::string_view device) noexcept -> std::string {
    return gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -o UUID '{}' | awk 'NR==2'"), device));
}

auto get_device_fstype(std::string_view device) noexcept -> std::string {
    const auto& cmd = fmt::format(FMT_COMPILE("lsblk -no FSTYPE '{}' | awk 'NR==1'"), device);
    return std::string{gucc::utils::trim(gucc::utils::exec(cmd))};
}

}  // namespace gucc::fs::utils
