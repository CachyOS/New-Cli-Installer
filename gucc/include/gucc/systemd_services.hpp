#ifndef SYSTEMD_SERVICES_HPP
#define SYSTEMD_SERVICES_HPP

#include "gucc/export.hpp"

#include "gucc/error.hpp"

#include <string_view>  // for string_view

GUCC_EXPORT_BEGIN

namespace gucc::services {

// Enables systemd service on the system
auto enable_systemd_service(std::string_view service_name, std::string_view root_mountpoint) noexcept -> Result<void>;

// Disables systemd service on the system
auto disable_systemd_service(std::string_view service_name, std::string_view root_mountpoint) noexcept -> Result<void>;

// Enables a user-level systemd service globally (--global)
auto enable_user_systemd_service(std::string_view service_name, std::string_view root_mountpoint) noexcept -> Result<void>;

/// Whether unit is active on the host.
auto is_active(std::string_view unit) noexcept -> bool;

// Checks if a systemd unit file exists on the target system
auto systemd_unit_exists(std::string_view unit_name, std::string_view root_mountpoint) noexcept -> bool;

}  // namespace gucc::services

GUCC_EXPORT_END

#endif  // SYSTEMD_SERVICES_HPP
