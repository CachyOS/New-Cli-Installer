#ifndef CACHYOS_INSTALLER_SYSTEM_HPP
#define CACHYOS_INSTALLER_SYSTEM_HPP

#include "cachyos/types.hpp"

#include <chrono>    // for seconds
#include <expected>  // for expected
#include <string>    // for string

namespace cachyos::installer {

/// Detects whether the system is UEFI or BIOS and the init system.
[[nodiscard]] auto detect_system() noexcept -> std::expected<SystemInfo, std::string>;

/// Returns true if running as root.
[[nodiscard]] auto check_root() noexcept -> bool;

/// Returns true if there is an active network connection.
[[nodiscard]] auto is_connected() noexcept -> bool;

/// Blocks until a connection is established or timeout expires.
/// @param timeout_secs Maximum seconds to wait.
/// @return true if connected within the timeout.
[[nodiscard]] auto wait_for_connection(const std::chrono::seconds& timeout_secs) noexcept -> bool;

/// Installs CachyOS repos on the live system and refreshes pacman databases.
[[nodiscard]] auto install_cachyos_repo() noexcept -> std::expected<void, std::string>;

}  // namespace cachyos::installer

#endif  // CACHYOS_INSTALLER_SYSTEM_HPP
