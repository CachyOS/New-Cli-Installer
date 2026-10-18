#include "gucc/conf.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>  // for any_of
#include <ranges>     // for ranges::*

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_view_literals;

namespace gucc::conf {

auto set_directive(std::string_view content, std::string_view key, std::string_view directive, KeyStyle style) noexcept -> std::string {
    const auto matches = [key, style](std::string_view line) { return is_directive(line, key, style); };
    if (!std::ranges::any_of(utils::split_lines(content), matches)) {
        return fmt::format(FMT_COMPILE("{}{}{}\n"), content, (content.empty() || content.ends_with('\n')) ? ""sv : "\n"sv, directive);
    }
    return utils::map_lines(content, [&](std::string_view line) {
        return matches(line) ? std::string{directive} : std::string{line};
    });
}

auto set_value(std::string_view content, std::string_view section, std::string_view key, std::string_view value) noexcept -> std::string {
    auto lines            = utils::split_lines(content) | std::ranges::to<std::vector<std::string_view>>();
    const auto assignment = fmt::format(FMT_COMPILE("{}={}"), key, value);

    // a trailing newline shows as empty last line
    const bool had_trailing_newline = !lines.empty() && lines.back().empty();
    if (had_trailing_newline) {
        lines.pop_back();
    }

    bool in_section{false};
    std::optional<std::size_t> section_end{};
    for (auto&& [index, line] : std::ranges::views::enumerate(lines)) {
        const auto position = static_cast<std::size_t>(index);
        if (const auto name = section_name(line)) {
            in_section = *name == section;
            if (in_section) {
                section_end = position;
            }
            continue;
        }
        if (!in_section) {
            continue;
        }

        if (is_directive(line, key, KeyStyle::Assign, "#;"sv)) {
            line = assignment;
            if (had_trailing_newline) {
                lines.emplace_back();
            }
            return utils::join(lines);
        }
        if (!utils::trim(line).empty()) {
            section_end = position;
        }
    }

    const auto header = fmt::format(FMT_COMPILE("[{}]"), section);
    if (section_end) {
        lines.insert(utils::index_viewable_range(lines, static_cast<std::ptrdiff_t>(*section_end) + 1), assignment);
    } else {
        if (!lines.empty()) {
            lines.emplace_back();
        }
        lines.emplace_back(header);
        lines.emplace_back(assignment);
    }
    lines.emplace_back();
    return utils::join(lines);
}

}  // namespace gucc::conf
