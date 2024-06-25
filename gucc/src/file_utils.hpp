#ifndef FILE_UTILS_HPP
#define FILE_UTILS_HPP

#include <string>       // for string
#include <string_view>  // for string_view

namespace gucc::file_utils {

auto read_whole_file(const std::string_view& filepath) noexcept -> std::string;
bool write_to_file(const std::string_view& data, const std::string_view& filepath) noexcept;

}  // namespace gucc::file_utils

#endif  // FILE_UTILS_HPP
