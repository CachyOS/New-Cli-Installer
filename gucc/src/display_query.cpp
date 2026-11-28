#include "gucc/display_query.hpp"
#include "gucc/io_utils.hpp"

#include "detail/lsblk_json.hpp"

#include <array>  // for array

#include <rapidjson/document.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

using gucc::display::detail::parse_panel_orientation;

// indexed by DRM orientation
constexpr std::array<std::uint16_t, 4> kDegrees{0, 180, 270, 90};

constexpr auto get_rotation_val() noexcept {
    return gucc::utils::query_output({"/usr/bin/drm_info"s, "-j"s})
        .and_then(parse_panel_orientation);
}

}  // namespace

namespace gucc::display::detail {

auto parse_panel_orientation(std::string_view drm_info_json) noexcept -> std::optional<std::uint64_t> {
    namespace json = gucc::detail::lsblk_json;

    rapidjson::Document document;
    if (!json::parse(document, drm_info_json)) {
        return std::nullopt;
    }
    // top level is keyed by card node, only the first connector of each card is a panel candidate
    for (const auto& card : document.GetObject()) {
        const auto connectors = json::array(card.value, "connectors"sv);
        if (connectors.Empty()) {
            continue;
        }
        if (const auto* orientation = json::member(json::member(*connectors.Begin(), "properties"sv), "panel orientation"sv)) {
            return json::uint64(*orientation, "value"sv);
        }
    }
    return std::nullopt;
}

}  // namespace gucc::display::detail

namespace gucc::display {

auto get_panel_rotation() noexcept -> std::optional<std::uint16_t> {
    const auto orientation = get_rotation_val();
    if (!orientation || *orientation == 0 || *orientation >= kDegrees.size()) {
        return std::nullopt;
    }
    return kDegrees.at(*orientation);
}

}  // namespace gucc::display
