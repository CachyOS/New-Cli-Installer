#pragma once

#include "gucc/export.hpp"

#include "gucc/error.hpp"

#include <string_view>  // for string_view

GUCC_EXPORT_BEGIN

namespace gucc::repos {

// Imports and locally signs the CachyOS key from a keyserver.
auto install_cachyos_keyring() noexcept -> Result<void>;

// Refresh pacman keyring to make sure it is up to date befor pacstrap.
auto refresh_keyrings() noexcept -> Result<void>;

// Creates pacman.conf for target.
auto create_target_pacman_config(std::string_view base_config, std::string_view output_config) noexcept -> Result<void>;

/// Re-initializes and populates the pacman keyrings.
auto reset_target_keyring(std::string_view root) noexcept -> Result<void>;

}  // namespace gucc::repos

GUCC_EXPORT_END
