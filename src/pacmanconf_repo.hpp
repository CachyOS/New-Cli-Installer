#ifndef PACMANCONF_REPO_HPP
#define PACMANCONF_REPO_HPP

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace detail::pacmanconf {
bool push_repos_front(const std::string_view& file_path, const std::string_view& value) noexcept;
auto get_repo_list(const std::string_view& file_path) noexcept -> std::vector<std::string>;
}  // namespace detail::pacmanconf

#endif  // PACMANCONF_REPO_HPP
