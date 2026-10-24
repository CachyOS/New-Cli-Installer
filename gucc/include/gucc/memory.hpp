#pragma once

#include "gucc/cctype_wrapper.hpp"

#include <cstdint>      // for uint64_t
#include <limits>       // for numeric_limits
#include <optional>     // for optional
#include <string_view>  // for string_view

namespace gucc::memory {

inline constexpr std::uint64_t kKiB = 1024;
inline constexpr std::uint64_t kMiB = kKiB << 10U;
inline constexpr std::uint64_t kGiB = kMiB << 10U;
inline constexpr std::uint64_t kTiB = kGiB << 10U;

// Returns current system memory(RAM) in bytes, otherwise zero
[[nodiscard]] auto total_bytes() noexcept -> std::uint64_t;

[[nodiscard]] constexpr auto unit_multiplier(char unit) noexcept -> std::optional<std::uint64_t> {
    constexpr std::string_view kUnits{"KMGT"};
    const auto index = kUnits.find(utils::to_upper(unit));
    if (index == std::string_view::npos) {
        return std::nullopt;
    }
    return std::uint64_t{1} << (10U * (index + 1));
}

// nullopt on overflow
[[nodiscard]] constexpr auto scale_bytes(std::uint64_t value, std::uint64_t multiplier) noexcept -> std::optional<std::uint64_t> {
    if (multiplier != 0 && value > std::numeric_limits<std::uint64_t>::max() / multiplier) {
        return std::nullopt;
    }
    return value * multiplier;
}

// nullopt for invalid UB conversions
[[nodiscard]] auto scale_fractional_bytes(double value, std::uint64_t multiplier) noexcept -> std::optional<std::uint64_t>;

}  // namespace gucc::memory
