#pragma once

#include "cachyos/types.hpp"

#include <cstddef>      // for size_t
#include <string_view>  // for string_view

namespace cachyos::installer {

/// Returns true if a filesystem is mounted at the given mountpoint.
[[nodiscard]] auto check_mount(std::string_view mountpoint) noexcept -> bool;

/// Returns true if a base system is installed at the given mountpoint.
[[nodiscard]] auto check_base_installed(std::string_view mountpoint) noexcept -> bool;

[[nodiscard]] auto fstab_has_entries(std::string_view fstab_content) noexcept -> bool;

/// Missing user.
[[nodiscard]] auto shadow_password_empty(std::string_view shadow_content, std::string_view user) noexcept -> bool;

/// Runs all pre-install validation checks.
[[nodiscard]] auto final_check(const InstallContext& ctx) noexcept -> ValidationResult;

}  // namespace cachyos::installer

namespace cachyos::installer::detail {

[[nodiscard]] auto count_generated_locales(std::string_view locale_a_output) noexcept -> std::size_t;

}  // namespace cachyos::installer::detail
