#include "gucc/systemd_services.hpp"
#include "gucc/io_utils.hpp"

#include <filesystem>  // for exists
#include <string>      // for string

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace gucc::services {

auto enable_systemd_service(std::string_view service_name, std::string_view root_mountpoint) noexcept -> bool {
    const auto& systemctl_cmd = fmt::format(FMT_COMPILE("systemctl enable {}"), service_name);
    if (!utils::arch_chroot_checked(systemctl_cmd, root_mountpoint)) {
        spdlog::error("Failed to enable systemd service on {}: {}", root_mountpoint, service_name);
        return false;
    }
    return true;
}

auto disable_systemd_service(std::string_view service_name, std::string_view root_mountpoint) noexcept -> bool {
    const auto& systemctl_cmd = fmt::format(FMT_COMPILE("systemctl disable {}"), service_name);
    if (!utils::arch_chroot_checked(systemctl_cmd, root_mountpoint)) {
        spdlog::error("Failed to disable systemd service on {}: {}", root_mountpoint, service_name);
        return false;
    }
    return true;
}

auto enable_user_systemd_service(std::string_view service_name, std::string_view root_mountpoint) noexcept -> bool {
    const auto& systemctl_cmd = fmt::format(FMT_COMPILE("systemctl --global enable {}"), service_name);
    if (!utils::arch_chroot_checked(systemctl_cmd, root_mountpoint)) {
        spdlog::error("Failed to enable user systemd service on {}: {}", root_mountpoint, service_name);
        return false;
    }
    return true;
}

auto systemd_unit_exists(std::string_view unit_name, std::string_view root_mountpoint) noexcept -> bool {
    auto full_name = std::string{unit_name};
    if (!unit_name.contains('.')) {
        full_name += ".service";
    }
    return fs::exists(fmt::format(FMT_COMPILE("{}/usr/lib/systemd/system/{}"), root_mountpoint, full_name));
}

}  // namespace gucc::services
