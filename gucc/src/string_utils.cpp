#include "gucc/string_utils.hpp"

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
    // TODO(vnepogodin): refactor with our join
    std::string res{};
    for (const auto& line : multiline) {
        res += line;
        res += delim.data();
    }

    if (reverse) {
        std::ranges::reverse(res);
    }

    return res;
}

auto join(const std::vector<std::string>& lines, std::string_view delim) noexcept -> std::string {
    return lines | std::ranges::views::join_with(delim) | std::ranges::to<std::string>();
}

}  // namespace gucc::utils
