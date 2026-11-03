#pragma once

#include "gucc/export.hpp"

#include "gucc/error.hpp"

#include <optional>     // for optional
#include <span>         // for span
#include <string>       // for string
#include <string_view>  // for string_view

GUCC_EXPORT_BEGIN

namespace gucc::pacman {

/// Whether pkg is installed on the host.
auto is_installed(std::string_view pkg, std::optional<std::string_view> root = std::nullopt) noexcept -> bool;

/// Installs pkgs on the host.
auto install(std::span<const std::string> pkgs) noexcept -> Result<void>;

/// Installs pkg on the host if needed.
auto ensure_installed(std::string_view pkg) noexcept -> Result<void>;

/// Removes pkgs with their unneeded dependencies inside root.
auto remove(std::span<const std::string> pkgs, std::string_view root) noexcept -> Result<void>;

}  // namespace gucc::pacman

GUCC_EXPORT_END
