#ifndef REPOS_HPP
#define REPOS_HPP

#include "gucc/error.hpp"

namespace gucc::repos {

// Modifies host pacman.conf and installs keyring on the host
auto install_cachyos_repos() noexcept -> Result<void>;

}  // namespace gucc::repos

#endif  // REPOS_HPP
