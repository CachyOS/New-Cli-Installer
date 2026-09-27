#pragma once

#include <algorithm>   // for find, find_if
#include <functional>  // for identity, ref
#include <optional>    // for optional
#include <ranges>      // for ranges::*

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

// NOLINTEND(cppcoreguidelines-missing-std-forward)

}  // namespace gucc::utils
