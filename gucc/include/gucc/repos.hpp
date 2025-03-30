#ifndef REPOS_HPP
#define REPOS_HPP

namespace gucc::repos {

// Modifies host pacman.conf and installs keyring on the host
auto install_cachyos_repos() noexcept -> bool;

}  // namespace gucc::repos

#endif  // REPOS_HPP
