#ifndef SYSTEMD_SERVICES_HPP
#define SYSTEMD_SERVICES_HPP

#include <string_view>  // for string_view

namespace gucc::services {

// Enables systemd service on the system
auto enable_systemd_service(std::string_view service_name, std::string_view root_mountpoint) noexcept -> bool;

}  // namespace gucc::services

#endif  // SYSTEMD_SERVICES_HPP
