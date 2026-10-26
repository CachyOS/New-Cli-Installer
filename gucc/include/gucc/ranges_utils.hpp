#pragma once

#include <cstddef>  // for size_t

#include <algorithm>    // for find, find_if
#include <functional>   // for identity, ref
#include <memory>       // for addressof
#include <optional>     // for optional
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <type_traits>  // for is_lvalue_reference_v, remove_reference_t
#include <utility>      // for move

namespace gucc::utils {

// NOLINTBEGIN(cppcoreguidelines-missing-std-forward)
//
// forwarding a temporary container makes find return dangling

template <std::ranges::input_range R, class T, class Proj = std::identity>
constexpr auto find_optional(R&& range, const T& value, Proj proj = {}) -> std::optional<std::ranges::range_value_t<R>> {
    const auto it = std::ranges::find(range, value, std::ref(proj));
    if (it == std::ranges::end(range)) {
        return std::nullopt;
    }
    return *it;
}

template <std::ranges::input_range R, class Pred, class Proj = std::identity>
constexpr auto find_if_optional(R&& range, Pred pred, Proj proj = {}) -> std::optional<std::ranges::range_value_t<R>> {
    const auto it = std::ranges::find_if(range, std::ref(pred), std::ref(proj));
    if (it == std::ranges::end(range)) {
        return std::nullopt;
    }
    return *it;
}

/// Position of the first element matching @p pred.
template <std::ranges::forward_range R, class Pred, class Proj = std::identity>
constexpr auto index_if(R&& range, Pred pred, Proj proj = {}) -> std::optional<std::size_t> {
    const auto it = std::ranges::find_if(range, std::ref(pred), std::ref(proj));
    if (it == std::ranges::end(range)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::ranges::distance(std::ranges::begin(range), it));
}

/// Position of the first element equal to @p value.
template <std::ranges::forward_range R, class T, class Proj = std::identity>
constexpr auto index_of(R&& range, const T& value, Proj proj = {}) -> std::optional<std::size_t> {
    return index_if(range, [&value](const auto& elem) { return elem == value; }, std::move(proj));
}

// NOLINTEND(cppcoreguidelines-missing-std-forward)

template <std::ranges::forward_range R, class Pred, class Proj = std::identity>
    requires std::is_lvalue_reference_v<std::ranges::range_reference_t<R>>
constexpr auto find_if_ptr(R& range, Pred pred, Proj proj = {}) -> std::remove_reference_t<std::ranges::range_reference_t<R>>* {
    const auto it = std::ranges::find_if(range, std::ref(pred), std::ref(proj));
    return it == std::ranges::end(range) ? nullptr : std::addressof(*it);
}

template <std::ranges::forward_range R, class T, class Proj = std::identity>
    requires std::is_lvalue_reference_v<std::ranges::range_reference_t<R>>
constexpr auto find_ptr(R& range, const T& value, Proj proj = {}) -> std::remove_reference_t<std::ranges::range_reference_t<R>>* {
    return find_if_ptr(range, [&value](const auto& elem) { return elem == value; }, std::move(proj));
}

// smth smth Rust inspired overload for owned view
inline constexpr auto to_owned = std::ranges::views::transform([](std::string_view str) { return std::string{str}; });

}  // namespace gucc::utils
