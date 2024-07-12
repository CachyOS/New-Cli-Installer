#ifndef STRING_UTILS_HPP
#define STRING_UTILS_HPP

#include <algorithm>    // for transform
#include <concepts>     // for same_as
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::utils {

/// @brief Split a string into multiple lines based on a delimiter.
/// @param str The string to split.
/// @param reverse Flag indicating whether to reverse the order of the resulting lines.
/// @param delim The delimiter to split the string.
/// @return A vector of strings representing the split lines.
auto make_multiline(std::string_view str, bool reverse = false, char delim = '\n') noexcept -> std::vector<std::string>;

/// @brief Split a string into views of multiple lines based on a delimiter.
/// @param str The string to split.
/// @param reverse Flag indicating whether to reverse the order of the resulting lines.
/// @param delim The delimiter to split the string.
/// @return A vector of string views representing the split lines.
auto make_multiline_view(std::string_view str, bool reverse = false, char delim = '\n') noexcept -> std::vector<std::string_view>;

/// @brief Combine multiple lines into a single string using a delimiter.
/// @param multiline The lines to combine.
/// @param reverse Flag indicating whether to reverse the order of the lines before combining.
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

template <typename T>
concept string_findable = requires(T value) {
    // check that type of T::contains is bool
    { value.contains(std::string_view{""}) } -> std::same_as<bool>;
};
// simple helper function to check if string contains a string
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

}  // namespace gucc::utils

#endif  // STRING_UTILS_HPP
