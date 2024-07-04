#include "gucc/systemd_services.hpp"
#include "gucc/io_utils.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

namespace gucc::services {

auto enable_systemd_service(std::string_view service_name, std::string_view root_mountpoint) noexcept -> bool {
    // Enable service
    const auto& systemctl_cmd = fmt::format(FMT_COMPILE("systemctl enable {}"), service_name);
    if (!utils::arch_chroot_checked(systemctl_cmd, root_mountpoint)) {
        spdlog::error("Failed to enable systemd service on {}: {}", root_mountpoint, service_name);
        return false;
    }

    return true;
}

}  // namespace gucc::services
