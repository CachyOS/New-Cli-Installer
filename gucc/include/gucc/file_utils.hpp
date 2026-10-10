#ifndef FILE_UTILS_HPP
#define FILE_UTILS_HPP

#include "gucc/error.hpp"
#include "gucc/string_utils.hpp"

#include <concepts>     // for invocable
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for forward

namespace gucc::file_utils {

auto read_whole_file(std::string_view filepath) noexcept -> std::string;
auto read_first_line(std::string_view filepath) noexcept -> std::string;
auto write_to_file(std::string_view data, std::string_view filepath) noexcept -> bool;

// If the file doesn't exist, then it create one and write into it.
// If the file exists already, then it will overwrite file content with provided data.
auto create_file_for_overwrite(std::string_view filepath, std::string_view data) noexcept -> bool;

// Same as the bool helpers above, but fail with FileIo naming the path.
auto write_file(std::string_view filepath, std::string_view data) noexcept -> Result<void>;
auto create_directories(std::string_view path) noexcept -> Result<void>;
/// Overwrites @p to if it exists.
auto copy_file(std::string_view from, std::string_view to) noexcept -> Result<void>;

/// Rewrites @p filepath with functor.
template <std::invocable<std::string_view> Fn>
auto rewrite_lines(std::string_view filepath, Fn&& fn) noexcept -> Result<void> {
    return write_file(filepath, utils::map_lines(read_whole_file(filepath), std::forward<Fn>(fn)));
}

}  // namespace gucc::file_utils

#endif  // FILE_UTILS_HPP
