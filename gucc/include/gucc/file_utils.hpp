#ifndef FILE_UTILS_HPP
#define FILE_UTILS_HPP

#include <string>       // for string
#include <string_view>  // for string_view

namespace gucc::file_utils {

auto read_whole_file(std::string_view filepath) noexcept -> std::string;
auto write_to_file(std::string_view data, std::string_view filepath) noexcept -> bool;

// If the file doesn't exist, then it create one and write into it.
// If the file exists already, then it will overwrite file content with provided data.
auto create_file_for_overwrite(std::string_view filepath, std::string_view data) noexcept -> bool;

}  // namespace gucc::file_utils

#endif  // FILE_UTILS_HPP
