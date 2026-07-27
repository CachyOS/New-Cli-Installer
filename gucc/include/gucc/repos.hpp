#ifndef REPOS_HPP
#define REPOS_HPP

#include "gucc/error.hpp"

#include <string_view>  // for string_view

namespace gucc::repos {

// Installs keyring on the host.
auto install_cachyos_keyring() noexcept -> Result<void>;

// Creates pacman.conf for target.
auto create_target_pacman_config(std::string_view base_config, std::string_view output_config) noexcept -> Result<void>;

}  // namespace gucc::repos

#endif  // REPOS_HPP
