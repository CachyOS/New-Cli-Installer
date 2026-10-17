#pragma once

#include "gucc/string_utils.hpp"

#include <concepts>     // for predicate
#include <cstdint>      // for uint8_t
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view

#include <fmt/compile.h>
#include <fmt/format.h>

/// Line-level helpers for the common key value pairs in config files for certain software.
namespace gucc::conf {

struct Line {
    /// Text after the comment marker, not trimmed.
    std::string_view body;
    bool commented{};
};

[[nodiscard]] constexpr auto classify(std::string_view line, std::string_view markers = "#") noexcept -> std::optional<Line> {
    line = utils::ltrim(line);
    if (utils::rtrim(line).empty()) {
        return std::nullopt;
    }
    if (markers.contains(line.front())) {
        return Line{.body = line.substr(1), .commented = true};
    }
    return Line{.body = line, .commented = false};
}

/// Text of an uncommented, non-blank line.
[[nodiscard]] constexpr auto active(std::string_view line, std::string_view markers = "#") noexcept -> std::optional<std::string_view> {
    const auto parsed = classify(line, markers);
    return parsed && !parsed->commented ? std::optional{parsed->body} : std::nullopt;
}

/// `[name]` sort of field.
[[nodiscard]] constexpr auto section_name(std::string_view line) noexcept -> std::optional<std::string_view> {
    line = utils::trim(line.substr(0, line.find('#')));
    if (line.size() <= 2 || !line.starts_with('[') || !line.ends_with(']')) {
        return std::nullopt;
    }
    return line.substr(1, line.size() - 2);
}

enum class KeyStyle : std::uint8_t {
    /// `key = value`
    Assign,
    /// `key value`
    Word,
};

/// Empty when @p body carries no key in @p style.
[[nodiscard]] constexpr auto key_of(std::string_view body, KeyStyle style) noexcept -> std::string_view {
    if (style == KeyStyle::Word) {
        return utils::nth_field(body, 0, utils::kBlankChars);
    }
    return utils::split_once(body, '=').transform([](auto kv) { return utils::trim(kv.first); }).value_or(std::string_view{});
}

/// Active or commented directive for @p key.
[[nodiscard]] constexpr auto is_directive(std::string_view line, std::string_view key, KeyStyle style, std::string_view markers = "#") noexcept -> bool {
    const auto parsed = classify(line, markers);
    return parsed && key_of(utils::ltrim(parsed->body), style) == key;
}

/// Replaces every active or commented @p key directive with @p directive.
[[nodiscard]] auto set_directive(std::string_view content, std::string_view key, std::string_view directive, KeyStyle style) noexcept -> std::string;

/// Sets `key=value` in ini @p section.
[[nodiscard]] auto set_value(std::string_view content, std::string_view section, std::string_view key, std::string_view value) noexcept -> std::string;

/// Drops the comment marker of every commented line matching @p pred.
template <std::predicate<const Line&> Pred>
[[nodiscard]] auto uncomment_where(std::string_view content, Pred pred) -> std::string {
    return utils::map_lines(content, [&pred](std::string_view line) {
        const auto parsed = classify(line);
        if (!parsed || !parsed->commented || !pred(*parsed)) {
            return std::string{line};
        }
        // classify only strips indentation before the marker
        const auto marker = line.size() - utils::ltrim(line).size();
        return fmt::format(FMT_COMPILE("{}{}"), line.substr(0, marker), line.substr(marker + 1));
    });
}

/// Prefixes every active line matching @p pred.
template <std::predicate<const Line&> Pred>
[[nodiscard]] auto comment_out_where(std::string_view content, Pred pred) -> std::string {
    return utils::map_lines(content, [&pred](std::string_view line) {
        const auto parsed = classify(line);
        return parsed && !parsed->commented && pred(*parsed) ? fmt::format(FMT_COMPILE("#{}"), line) : std::string{line};
    });
}

}  // namespace gucc::conf
