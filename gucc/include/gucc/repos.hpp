#pragma once

#include "gucc/error.hpp"

#include <string_view>  // for string_view

namespace gucc::repos {

// Imports and locally signs the CachyOS key from a keyserver.
auto install_cachyos_keyring() noexcept -> Result<void>;

// Refresh pacman keyring to make sure it is up to date befor pacstrap.
auto refresh_keyrings() noexcept -> Result<void>;

// Creates pacman.conf for target.
auto create_target_pacman_config(std::string_view base_config, std::string_view output_config) noexcept -> Result<void>;

}  // namespace gucc::repos
