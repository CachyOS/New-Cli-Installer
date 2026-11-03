#ifndef FETCH_FILE_HPP
#define FETCH_FILE_HPP

#include "gucc/export.hpp"

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view

GUCC_EXPORT_BEGIN

namespace gucc::fetch {

// Fetch a single file from url into memory
auto fetch_file(std::string_view url) noexcept -> std::optional<std::string>;

// Fetch file from url into memory
auto fetch_file_from_url(std::string_view url, std::string_view fallback_url) noexcept -> std::optional<std::string>;

}  // namespace gucc::fetch

GUCC_EXPORT_END

#endif  // FETCH_FILE_HPP
