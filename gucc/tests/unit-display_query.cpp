#include "doctest_compatibility.h"

#include "gucc/display_query.hpp"

#include <optional>

using namespace std::string_view_literals;

TEST_CASE("drm info")
{
    using gucc::display::detail::parse_panel_orientation;

    SECTION("first card with connectors wins")
    {
        static constexpr auto kDrmInfo = R"({
  "/dev/dri/card0": {"driver": {"name": "simpledrm"}, "connectors": []},
  "/dev/dri/card1": {
    "driver": {"name": "amdgpu"},
    "connectors": [
      {"id": 80, "properties": {"panel orientation": {"value": 3, "type": 3}, "DPMS": {"value": 0}}},
      {"id": 90, "properties": {"panel orientation": {"value": 1}}}
    ]
  }
})"sv;
        CHECK_EQ(parse_panel_orientation(kDrmInfo), std::optional<std::uint64_t>{3});
    }
    SECTION("no orientation property")
    {
        CHECK_FALSE(parse_panel_orientation(R"({"/dev/dri/card0": {"connectors": [{"properties": {"DPMS": {"value": 0}}}]}})"sv).has_value());
    }
    SECTION("orientation without an unsigned value")
    {
        CHECK_FALSE(parse_panel_orientation(R"({"/dev/dri/card0": {"connectors": [{"properties": {"panel orientation": {"value": "x"}}}]}})"sv).has_value());
        CHECK_FALSE(parse_panel_orientation(R"({"/dev/dri/card0": {"connectors": [{"properties": {"panel orientation": {"value": -1}}}]}})"sv).has_value());
    }
    SECTION("no connectors or bad json")
    {
        CHECK_FALSE(parse_panel_orientation(R"({"/dev/dri/card0": {}})"sv).has_value());
        CHECK_FALSE(parse_panel_orientation("drm_info: command not found"sv).has_value());
    }
}
