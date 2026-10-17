#include "gucc/string_utils.hpp"

#include <cstddef>  // for size_t

#include <algorithm>    // for for_each, transform
#include <charconv>     // for from_chars
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::utils {

auto make_multiline(std::string_view str, bool reverse, char delim) noexcept -> std::vector<std::string> {
    auto lines = utils::make_split_view(str, delim) | std::ranges::to<std::vector<std::string>>();
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

auto make_multiline_view(std::string_view str, bool reverse, char delim) noexcept -> std::vector<std::string_view> {
    auto lines = utils::make_split_view(str, delim) | std::ranges::to<std::vector<std::string_view>>();
    if (reverse) {
        std::ranges::reverse(lines);
    }
    return lines;
}

auto decode_escapes(std::string_view str, std::string_view intro, int base, std::size_t width) noexcept -> std::string {
    if (intro.empty()) {
        return std::string{str};
    }
    std::string result{};
    result.reserve(str.size());
    for (auto escape = str.find(intro); escape != std::string_view::npos; escape = str.find(intro)) {
        result.append(str.substr(0, escape));
        str.remove_prefix(escape + intro.size());

        unsigned char byte{};
        const auto code = str.substr(0, width);
        if (const auto [end, ec] = std::from_chars(code.data(), code.data() + code.size(), byte, base);
            code.size() == width && ec == std::errc{} && end == code.data() + code.size()) {
            result.push_back(static_cast<char>(byte));
            str.remove_prefix(code.size());
        } else {
            result.append(intro);
        }
    }
    result.append(str);
    return result;
}

}  // namespace gucc::utils
