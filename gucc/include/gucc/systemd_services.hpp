#ifndef SYSTEMD_SERVICES_HPP
#define SYSTEMD_SERVICES_HPP

#include <string_view>  // for string_view

namespace gucc::services {

// Enables systemd service on the system
auto enable_systemd_service(std::string_view service_name, std::string_view root_mountpoint) noexcept -> bool;

// Disables systemd service on the system
auto disable_systemd_service(std::string_view service_name, std::string_view root_mountpoint) noexcept -> bool;

// Enables a user-level systemd service globally (--global)
auto enable_user_systemd_service(std::string_view service_name, std::string_view root_mountpoint) noexcept -> bool;

// Checks if a systemd unit file exists on the target system
auto systemd_unit_exists(std::string_view unit_name, std::string_view root_mountpoint) noexcept -> bool;

}  // namespace gucc::services

#endif  // SYSTEMD_SERVICES_HPP
