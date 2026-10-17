#ifndef STRING_UTILS_HPP
#define STRING_UTILS_HPP

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
auto join(const std::vector<std::string>& lines, char delim = '\n') noexcept -> std::string;

/// @brief Make a split view from a string into multiple lines based on a delimiter.
/// @param str The string to split.
/// @param delim The delimiter to split the string.
/// @return A range view representing the split lines.
constexpr auto make_split_view(std::string_view str, char delim = '\n') noexcept {
    constexpr auto functor = [](auto&& rng) {
        return std::string_view(&*rng.begin(), static_cast<size_t>(std::ranges::distance(rng)));
    };
    constexpr auto second = [](auto&& rng) { return rng != ""; };

    return str
        | std::ranges::views::split(delim)
        | std::ranges::views::transform(functor)
        | std::ranges::views::filter(second);
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

/// True when @p line starts with @p word followed by one of @p delims or the end.
constexpr auto starts_with_word(std::string_view line, std::string_view word, std::string_view delims = " \t") noexcept -> bool {
    return line.starts_with(word) && (line.size() == word.size() || delims.contains(line[word.size()]));
}

/// Field @p n of @p line split on any char of @p delims.
constexpr auto nth_field(std::string_view line, std::size_t n, std::string_view delims = " \t") noexcept -> std::string_view {
    const auto is_delim = [delims](char ch) { return delims.contains(ch); };
    auto fields         = line
        | std::ranges::views::chunk_by([is_delim](char lhs, char rhs) { return is_delim(lhs) == is_delim(rhs); })
        | std::ranges::views::filter([is_delim](auto&& run) { return !is_delim(run.front()); })
        | std::ranges::views::drop(n);
    const auto it = std::ranges::begin(fields);
    return it == std::ranges::end(fields) ? std::string_view{} : std::string_view{*it};
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
    const auto [in, out] = std::ranges::copy(split_lines(line, delim) | std::ranges::views::take(N), fields.begin());
    if (out != fields.end()) {
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
