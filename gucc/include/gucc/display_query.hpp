#pragma once

#include "gucc/export.hpp"

#include <cstdint>      // for uint16_t, uint64_t
#include <optional>     // for optional
#include <string_view>  // for string_view

GUCC_EXPORT_BEGIN

namespace gucc::display {

/// @brief Clockwise degrees (90, 180, 270) that turn output on a panel mounted
/// rotated in its casing.
/// @return std::nullopt for an upright or unknown panel.
auto get_panel_rotation() noexcept -> std::optional<std::uint16_t>;

}  // namespace gucc::display

namespace gucc::display::detail {

auto parse_panel_orientation(std::string_view drm_info_json) noexcept -> std::optional<std::uint64_t>;

}  // namespace gucc::display::detail

GUCC_EXPORT_END
