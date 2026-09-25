#pragma once

#include <string>       // for string
#include <string_view>  // for string_view

namespace gucc::file_utils {

[[nodiscard]] auto read_whole_file(std::string_view filepath) noexcept -> std::string;
[[nodiscard]] auto read_first_line(std::string_view filepath) noexcept -> std::string;
auto write_to_file(std::string_view data, std::string_view filepath) noexcept -> bool;
auto create_file_for_overwrite(std::string_view filepath, std::string_view data) noexcept -> bool;

}  // namespace gucc::file_utils
