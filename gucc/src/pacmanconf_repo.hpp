#ifndef PACMANCONF_REPO_HPP
#define PACMANCONF_REPO_HPP

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::detail::pacmanconf {
bool push_repos_front(std::string_view file_path, std::string_view value) noexcept;
auto get_repo_list(std::string_view file_path) noexcept -> std::vector<std::string>;
}  // namespace gucc::detail::pacmanconf

#endif  // PACMANCONF_REPO_HPP
