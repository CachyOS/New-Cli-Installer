#ifndef CACHYOS_INSTALLER_CONFIG_HPP
#define CACHYOS_INSTALLER_CONFIG_HPP

#include "cachyos/types.hpp"

#include <expected>     // for expected
#include <string>       // for string
#include <string_view>  // for string_view

namespace cachyos::installer {

/// Applies system settings (hostname, locale, keymap, timezone, hw_clock) to the installed system.
[[nodiscard]] auto apply_system_settings(const SystemSettings& settings, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string>;

/// Creates a new user account on the installed system.
[[nodiscard]] auto create_user(const UserSettings& settings, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string>;

/// Sets the root password on the installed system.
[[nodiscard]] auto set_root_password(std::string_view password, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string>;

/// Enables autologin for the specified display manager and user.
[[nodiscard]] auto enable_autologin(std::string_view dm, std::string_view username, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string>;

}  // namespace cachyos::installer

#endif  // CACHYOS_INSTALLER_CONFIG_HPP
