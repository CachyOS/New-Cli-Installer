#pragma once

#include "gucc/error.hpp"

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::detail::pacmanconf {

/// Inserts @p value before the first repo section.
auto push_repos_front(std::string_view file_path, std::string_view value) noexcept -> Result<void>;
auto get_repo_list(std::string_view file_path) noexcept -> std::vector<std::string>;

[[nodiscard]] auto repo_headers(std::string_view content) noexcept -> std::vector<std::string>;

/// Puts @p value on its own lines ahead of the first repo header.
[[nodiscard]] auto insert_before_first_repo(std::string_view content, std::string_view value) noexcept -> std::optional<std::string>;

}  // namespace gucc::detail::pacmanconf
