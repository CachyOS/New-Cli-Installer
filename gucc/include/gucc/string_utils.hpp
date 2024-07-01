#ifndef STRING_UTILS_HPP
#define STRING_UTILS_HPP

#include <algorithm>    // for transform
#include <string>       // for string
#include <string_view>  // for string_view
#include <type_traits>
#include <vector>  // for vector

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wsign-conversion"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <range/v3/iterator/operations.hpp>
#include <range/v3/range/traits.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/split.hpp>
#include <range/v3/view/transform.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

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
auto join(const std::vector<std::string>& lines, std::string_view delim = "\n") noexcept -> std::string;

/// @brief Make a split view from a string into multiple lines based on a delimiter.
/// @param str The string to split.
/// @param delim The delimiter to split the string.
/// @return A range view representing the split lines.
constexpr auto make_split_view(std::string_view str, char delim = '\n') noexcept {
    constexpr auto functor = [](auto&& rng) {
        return std::string_view(&*rng.begin(), static_cast<size_t>(ranges::distance(rng)));
    };
    constexpr auto second = [](auto&& rng) { return rng != ""; };

    return str
        | ranges::views::split(delim)
        | ranges::views::transform(functor)
        | ranges::views::filter(second);
}

template <typename T, typename npos_type = std::remove_cvref_t<decltype(T::npos)>>
concept string_findable = requires(T value) {
    // check that type of T::npos is T::size_type
    { npos_type{} } -> std::same_as<typename T::size_type>;
    // check that type of T::find is T::size_type
    { value.find(std::string_view{""}) } -> std::same_as<typename T::size_type>;
};

// simple helper function to check if string contains a string
constexpr auto contains(string_findable auto const& str, std::string_view needle) noexcept -> bool {
    using str_type = std::remove_reference_t<decltype(str)>;
    return str.find(needle) != str_type::npos;
}

}  // namespace gucc::utils

#endif  // STRING_UTILS_HPP
