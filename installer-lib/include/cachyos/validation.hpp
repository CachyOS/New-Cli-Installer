#ifndef CACHYOS_INSTALLER_VALIDATION_HPP
#define CACHYOS_INSTALLER_VALIDATION_HPP

#include "cachyos/types.hpp"

#include <string_view>  // for string_view

namespace cachyos::installer {

/// Returns true if a filesystem is mounted at the given mountpoint.
[[nodiscard]] auto check_mount(std::string_view mountpoint) noexcept -> bool;

/// Returns true if a base system is installed at the given mountpoint.
[[nodiscard]] auto check_base_installed(std::string_view mountpoint) noexcept -> bool;

/// Runs all pre-install validation checks.
[[nodiscard]] auto final_check(const InstallContext& ctx) noexcept -> ValidationResult;

}  // namespace cachyos::installer

#endif  // CACHYOS_INSTALLER_VALIDATION_HPP
