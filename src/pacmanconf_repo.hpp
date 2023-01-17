#ifndef PACMANCONF_REPO_HPP
#define PACMANCONF_REPO_HPP

#include <string_view>  // for string_view

namespace detail::pacmanconf {
bool push_repos_front(const std::string_view& file_path, const std::string_view& value) noexcept;
}  // namespace detail::pacmanconf

#endif  // PACMANCONF_REPO_HPP
