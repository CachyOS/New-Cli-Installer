#include "gucc/string_utils.hpp"

#include <algorithm>    // for for_each, transform
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::utils {

auto make_multiline(std::string_view str, bool reverse, char delim) noexcept -> std::vector<std::string> {
    std::vector<std::string> lines{};
    std::ranges::for_each(utils::make_split_view(str, delim), [&](auto&& rng) { lines.emplace_back(rng); });
    if (reverse) {
        std::ranges::reverse(lines);
    }
    return lines;
}

auto make_multiline_view(std::string_view str, bool reverse, char delim) noexcept -> std::vector<std::string_view> {
    std::vector<std::string_view> lines{};
    std::ranges::for_each(utils::make_split_view(str, delim), [&](auto&& rng) { lines.emplace_back(rng); });
    if (reverse) {
        std::ranges::reverse(lines);
    }
    return lines;
}

auto make_multiline(const std::vector<std::string>& multiline, bool reverse, std::string_view delim) noexcept -> std::string {
    const auto join_terminated = [delim](auto&& lines) {
        return lines
            | std::ranges::views::transform([delim](std::string_view line) { return std::ranges::views::concat(line, delim); })
            | std::ranges::views::join
            | std::ranges::to<std::string>();
    };
    return reverse ? join_terminated(multiline | std::ranges::views::reverse) : join_terminated(multiline);
}

auto join(const std::vector<std::string>& lines, char delim) noexcept -> std::string {
    return lines | std::ranges::views::join_with(delim) | std::ranges::to<std::string>();
}

}  // namespace gucc::utils
