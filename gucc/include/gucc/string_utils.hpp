#ifndef STRING_UTILS_HPP
#define STRING_UTILS_HPP

#include "gucc/cctype_wrapper.hpp"

#include <cstddef>  // for size_t

#include <algorithm>    // for find_if_not
#include <array>        // for array
#include <charconv>     // for from_chars
#include <concepts>     // for invocable, same_as, unsigned_integral
#include <optional>     // for optional
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for forward, pair
#include <vector>       // for vector

namespace gucc::utils {

/// @brief Split a string into multiple lines based on a delimiter.
/// @param str The string to split.
/// @param reverse Flag indicating whether to reverse the order of the
///                resulting lines.
/// @param delim The delimiter to split the string.
/// @return A vector of strings representing the split lines.
auto make_multiline(std::string_view str, bool reverse = false, char delim = '\n') noexcept -> std::vector<std::string>;

/// @brief Split a string into views of multiple lines based on a delimiter.
/// @param str The string to split.
/// @param reverse Flag indicating whether to reverse the order of the
///                resulting lines.
/// @param delim The delimiter to split the string.
/// @return A vector of string views representing the split lines.
auto make_multiline_view(std::string_view str, bool reverse = false, char delim = '\n') noexcept -> std::vector<std::string_view>;

/// @brief Combine multiple lines into a single string using a delimiter.
/// @param multiline The lines to combine.
/// @param reverse Flag indicating whether to reverse the order of the lines
///                before combining.
/// @param delim The delimiter to join the lines.
/// @return The combined lines as a single string.
auto make_multiline(const std::vector<std::string>& multiline, bool reverse = false, std::string_view delim = "\n") noexcept -> std::string;

/// @brief Join a vector of strings into a single string using a delimiter.
/// @param lines The lines to join.
/// @param delim The delimiter to join the lines.
/// @return The joined lines as a single string.
template <std::ranges::input_range R, class Delim = char>
    requires std::convertible_to<std::ranges::range_reference_t<R>, std::string_view>
constexpr auto join(R&& lines, Delim delim = '\n') noexcept -> std::string {
    return std::forward<R>(lines)
        | std::ranges::views::transform([](std::string_view line) { return line; })
        | std::ranges::views::join_with(delim)
        | std::ranges::to<std::string>();
}

/// @brief Make a split view from a string into multiple lines based on a delimiter.
/// @param str The string to split.
/// @param delim The delimiter to split the string.
/// @return A range view representing the split lines.
template <class Delim = char>
    requires std::same_as<Delim, char> || std::same_as<Delim, std::string_view>
constexpr auto make_split_view(std::string_view str, Delim delim = '\n') noexcept {
    return str
        | std::ranges::views::split(delim)
        | std::ranges::views::transform([](auto&& piece) { return std::string_view{piece.begin(), piece.end()}; })
        | std::ranges::views::filter([](std::string_view piece) { return !piece.empty(); });
}

/// Unlike make_split_view, keeps empty lines, so joining them back restores the text.
constexpr auto split_lines(std::string_view str, char delim = '\n') noexcept {
    return str
        | std::ranges::views::split(delim)
        | std::ranges::views::transform([](auto&& line) { return std::string_view{line}; });
}

/// Maps every line through @p fn, keeping empty lines and line breaks as they were.
template <std::invocable<std::string_view> Fn>
constexpr auto map_lines(std::string_view str, Fn&& fn) -> std::string {
    return split_lines(str)
        | std::ranges::views::transform(std::forward<Fn>(fn))
        | std::ranges::views::join_with('\n')
        | std::ranges::to<std::string>();
}

/// @brief Concept representing a type that supports string find operations.
template <typename T>
concept string_findable = requires(T value) {
    // check that type of T::contains is bool
    { value.contains(std::string_view{""}) } -> std::same_as<bool>;
};

/// @brief Checks if a string contains a substring.
/// @param str The string to search in.
/// @param needle The substring to search for.
/// @return `true` if the string contains the substring, `false` otherwise.
constexpr auto contains(string_findable auto const& str, std::string_view needle) noexcept -> bool {
    return str.contains(needle);
}

template <std::ranges::viewable_range R>
constexpr auto index_viewable_range(R&& rng, std::ranges::range_difference_t<R> n) noexcept {
    return std::ranges::next(std::ranges::begin(rng), n);
}

template <std::ranges::viewable_range R>
constexpr auto size_viewable_range(R&& rng) noexcept {
    return std::ranges::distance(std::ranges::begin(rng), std::ranges::end(rng));
}

/// @brief Extract value after a prefix until delimiter.
/// @param str The string to search in.
/// @param prefix The prefix to search for.
/// @param delim The delimiter marking end of value(default: space).
/// @return The extracted value, or nullopt if prefix not found.
constexpr auto extract_after(std::string_view str, std::string_view prefix, char delim = ' ') noexcept
    -> std::optional<std::string_view> {
    if (auto pos = str.find(prefix); pos != std::string_view::npos) {
        auto start = pos + prefix.size();
        auto end   = str.find(delim, start);
        return str.substr(start, end == std::string_view::npos ? str.size() - start : end - start);
    }
    return std::nullopt;
}

/// The isspace set.
inline constexpr std::string_view kWhitespaceChars{" \f\n\r\t\v"};
/// Word separators.
inline constexpr std::string_view kBlankChars{" \t\r"};

/// @brief Trim leading whitespace from a string view.
/// @param str The string to trim.
/// @return The trimmed string view.
constexpr auto ltrim(std::string_view str) noexcept -> std::string_view {
    auto it       = std::ranges::find_if_not(str, [](char ch) { return kWhitespaceChars.contains(ch); });
    auto last_pos = std::ranges::distance(str.begin(), it);
    return str.substr(static_cast<size_t>(last_pos));
}

/// @brief Trim trailing whitespace from a string view.
/// @param str The string to trim.
/// @return The trimmed string view.
constexpr auto rtrim(std::string_view str) noexcept -> std::string_view {
    auto it       = std::ranges::find_if_not(str | std::ranges::views::reverse, [](char ch) { return kWhitespaceChars.contains(ch); });
    auto last_pos = std::ranges::distance(str.rbegin(), it);
    return str.substr(0, str.size() - static_cast<size_t>(last_pos));
}

/// @brief Trim both leading and trailing whitespace.
/// @param str The string to trim.
/// @return The trimmed string view.
constexpr auto trim(std::string_view str) noexcept -> std::string_view {
    return rtrim(ltrim(str));
}

/// @brief Strip prefix from string view.
/// @param str The string to work on.
/// @param prefix The string prefix.
/// @return The string view optional without prefix, std::nullopt otherwise.
[[nodiscard]] constexpr auto strip_prefix(std::string_view str, std::string_view prefix) noexcept -> std::optional<std::string_view> {
    return str.starts_with(prefix) ? std::optional{str.substr(prefix.size())} : std::nullopt;
}

/// @brief Strip suffix from string view.
/// @param str The string to work on.
/// @param suffix The string suffix.
/// @return The string view optional without suffix, std::nullopt otherwise.
[[nodiscard]] constexpr auto strip_suffix(std::string_view str, std::string_view suffix) noexcept -> std::optional<std::string_view> {
    return str.ends_with(suffix) ? std::optional{str.substr(0, str.size() - suffix.size())} : std::nullopt;
}

/// @p str between @p open and @p close.
[[nodiscard]] constexpr auto strip_enclosing(std::string_view str, std::string_view open, std::string_view close) noexcept -> std::optional<std::string_view> {
    if (str.size() < (open.size() + close.size())) {
        return std::nullopt;
    }
    return strip_prefix(str, open).and_then([close](std::string_view rest) { return strip_suffix(rest, close); });
}

/// @p str without one pair of matching single or double quotes, otherwise unchanged.
[[nodiscard]] constexpr auto unquote(std::string_view str) noexcept -> std::string_view {
    if (str.size() >= 2 && (str.front() == '"' || str.front() == '\'') && (str.back() == str.front())) {
        return str.substr(1, str.size() - 2);
    }
    return str;
}

[[nodiscard]] constexpr auto to_lower(std::string_view str) -> std::string {
    return str
        | std::ranges::views::transform([](char ch) { return ::gucc::utils::to_lower(ch); })
        | std::ranges::to<std::string>();
}

// NOTE: ASCII case-insensitive.
[[nodiscard]] constexpr auto iequals(std::string_view lhs, std::string_view rhs) noexcept -> bool {
    constexpr auto lower = [](char ch) { return ::gucc::utils::to_lower(ch); };
    return std::ranges::equal(lhs, rhs, {}, lower, lower);
}

/// @p path without trailing slashes.
[[nodiscard]] constexpr auto strip_trailing_slashes(std::string_view path) noexcept -> std::string_view {
    const auto last = path.find_last_not_of('/');
    return last == std::string_view::npos ? path.substr(0, std::min<std::size_t>(path.size(), 1)) : path.substr(0, last + 1);
}

/// Directory name without its trailing slashes.
[[nodiscard]] constexpr auto dirname(std::string_view path) noexcept -> std::string_view {
    const auto pos = path.find_last_of('/');
    return pos == std::string_view::npos ? std::string_view{} : strip_trailing_slashes(path.substr(0, pos + 1));
}

/// Host path of @p path inside the tree at @p root.
[[nodiscard]] constexpr auto target_path(std::string_view root, std::string_view path) -> std::string {
    const auto root_end   = root.find_last_not_of('/');
    const auto path_begin = path.find_first_not_of('/');
    std::string joined{(root_end == std::string_view::npos) ? std::string_view{} : root.substr(0, root_end + 1)};
    if (path_begin == std::string_view::npos) {
        // keep the root itself
        return joined.empty() ? std::string{root.empty() ? path : std::string_view{"/"}} : joined;
    }
    joined += '/';
    joined += path.substr(path_begin);
    return joined;
}

/// True when @p line starts with @p word followed by one of @p delims or the end.
constexpr auto starts_with_word(std::string_view line, std::string_view word, std::string_view delims = " \t") noexcept -> bool {
    return line.starts_with(word) && (line.size() == word.size() || delims.contains(line[word.size()]));
}

/// The blank-separated words of @p line.
constexpr auto words(std::string_view line, std::string_view delims = " \t") noexcept {
    const auto is_delim = [delims](char ch) { return delims.contains(ch); };
    return line
        | std::ranges::views::chunk_by([is_delim](char lhs, char rhs) { return is_delim(lhs) == is_delim(rhs); })
        | std::ranges::views::filter([is_delim](auto&& run) { return !is_delim(run.front()); })
        | std::ranges::views::transform([](auto&& run) { return std::string_view{run}; });
}

/// Field @p n of @p line split on any char of @p delims.
constexpr auto nth_field(std::string_view line, std::size_t n, std::string_view delims = " \t") noexcept -> std::string_view {
    auto fields   = words(line, delims) | std::ranges::views::drop(n);
    const auto it = std::ranges::begin(fields);
    return it == std::ranges::end(fields) ? std::string_view{} : *it;
}

constexpr auto first_field(std::string_view line) noexcept -> std::string_view {
    return nth_field(line, 0);
}

/// Splits at the first @p sep. nullopt without one.
constexpr auto split_once(std::string_view str, char sep) noexcept -> std::optional<std::pair<std::string_view, std::string_view>> {
    const auto pos = str.find(sep);
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    return std::pair{str.substr(0, pos), str.substr(pos + 1)};
}

/// The first @p N fields split on @p delim, empty ones included.
template <std::size_t N>
constexpr auto split_fields(std::string_view line, char delim) noexcept -> std::optional<std::array<std::string_view, N>> {
    std::array<std::string_view, N> fields{};
    if (line.empty()) {
        return N == 1 ? std::optional{fields} : std::nullopt;
    }
    if (std::ranges::copy(split_lines(line, delim) | std::ranges::views::take(N), fields.begin()).out != fields.end()) {
        return std::nullopt;
    }
    return fields;
}

constexpr auto key_value(std::string_view token, std::string_view key, char sep = '=') noexcept -> std::optional<std::string_view> {
    return split_once(token, sep).and_then([key](auto kv) -> std::optional<std::string_view> {
        return !key.empty() && kv.first == key ? std::optional{kv.second} : std::nullopt;
    });
}

template <std::ranges::input_range R>
constexpr auto value_of(R&& tokens, std::string_view key, char sep = '=') noexcept -> std::optional<std::string_view> {
    auto values = std::forward<R>(tokens)
        | std::ranges::views::transform([key, sep](std::string_view token) { return key_value(token, key, sep); })
        | std::ranges::views::join;
    const auto it = std::ranges::begin(values);
    return it == std::ranges::end(values) ? std::nullopt : std::optional<std::string_view>{*it};
}

/// Lines that hold more than whitespace, trimmed.
constexpr auto nonblank_lines(std::string_view str) noexcept {
    return split_lines(str)
        | std::ranges::views::transform(trim)
        | std::ranges::views::filter([](std::string_view line) { return !line.empty(); });
}

auto decode_escapes(std::string_view str, std::string_view intro, int32_t base, std::size_t width) noexcept -> std::string;

/// @brief Parse unsigned integer from string view.
/// @tparam T The unsigned integer type to parse into.
/// @param str The string to parse.
/// @return The parsed value, or nullopt on failure.
template <std::unsigned_integral T>
constexpr auto parse_uint(std::string_view str) noexcept -> std::optional<T> {
    T value{};
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
    if (ec == std::errc{}) {
        return value;
    }
    return std::nullopt;
}

/// Unlike parse_uint, the whole string must be number.
template <std::unsigned_integral T>
constexpr auto parse_uint_exact(std::string_view str) noexcept -> std::optional<T> {
    T value{};
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
    if (ec == std::errc{} && ptr == str.data() + str.size()) {
        return value;
    }
    return std::nullopt;
}

}  // namespace gucc::utils

#endif  // STRING_UTILS_HPP
