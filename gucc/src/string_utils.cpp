#include "gucc/string_utils.hpp"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <range/v3/algorithm/for_each.hpp>
#include <range/v3/algorithm/reverse.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/join.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace gucc::utils {

auto make_multiline(std::string_view str, bool reverse, char delim) noexcept -> std::vector<std::string> {
    std::vector<std::string> lines{};
    ranges::for_each(utils::make_split_view(str, delim), [&](auto&& rng) { lines.emplace_back(rng); });
    if (reverse) {
        ranges::reverse(lines);
    }
    return lines;
}

auto make_multiline_view(std::string_view str, bool reverse, char delim) noexcept -> std::vector<std::string_view> {
    std::vector<std::string_view> lines{};
    ranges::for_each(utils::make_split_view(str, delim), [&](auto&& rng) { lines.emplace_back(rng); });
    if (reverse) {
        ranges::reverse(lines);
    }
    return lines;
}

auto make_multiline(const std::vector<std::string>& multiline, bool reverse, std::string_view delim) noexcept -> std::string {
    std::string res{};
    for (const auto& line : multiline) {
        res += line;
        res += delim.data();
    }

    if (reverse) {
        ranges::reverse(res);
    }

    return res;
}

auto join(const std::vector<std::string>& lines, std::string_view delim) noexcept -> std::string {
    return lines | ranges::views::join(delim) | ranges::to<std::string>();
}

}  // namespace gucc::utils
