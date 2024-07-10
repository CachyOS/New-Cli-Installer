#ifndef FETCH_FILE_HPP
#define FETCH_FILE_HPP

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view

namespace gucc::fetch {

// Fetch file from url into memory
auto fetch_file_from_url(std::string_view url, std::string_view fallback_url) noexcept -> std::optional<std::string>;

}  // namespace gucc::fetch

#endif  // FETCH_FILE_HPP
