#ifndef STRING_UTILS_HPP
#define STRING_UTILS_HPP

#include <algorithm>    // for transform, contains, find_if_not
#include <array>        // for array
#include <charconv>     // for from_chars
#include <concepts>     // for same_as, unsigned_integral
#include <optional>     // for optional
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
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

/// @brief Trim leading whitespace from a string view.
/// @param str The string to trim.
/// @return The trimmed string view.
constexpr auto ltrim(std::string_view str) noexcept -> std::string_view {
    auto it       = std::ranges::find_if_not(str, [](unsigned char ch) {
        constexpr std::array spaces_ch{' ', '\f', '\n', '\r', '\t', '\v'};
        return std::ranges::contains(spaces_ch, ch);
    });
    auto last_pos = std::ranges::distance(str.begin(), it);
    return str.substr(static_cast<size_t>(last_pos));
}

/// @brief Trim trailing whitespace from a string view.
/// @param str The string to trim.
/// @return The trimmed string view.
constexpr auto rtrim(std::string_view str) noexcept -> std::string_view {
    auto it       = std::ranges::find_if_not(str | std::views::reverse, [](unsigned char ch) {
        constexpr std::array spaces_ch{' ', '\f', '\n', '\r', '\t', '\v'};
        return std::ranges::contains(spaces_ch, ch);
    });
    auto last_pos = std::ranges::distance(str.rbegin(), it);
    return str.substr(0, str.size() - static_cast<size_t>(last_pos));
}

/// @brief Trim both leading and trailing whitespace.
/// @param str The string to trim.
/// @return The trimmed string view.
constexpr auto trim(std::string_view str) noexcept -> std::string_view {
    return rtrim(ltrim(str));
}

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

}  // namespace gucc::utils

#endif  // STRING_UTILS_HPP
