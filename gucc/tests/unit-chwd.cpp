#include "doctest_compatibility.h"

#include "gucc/chwd.hpp"

#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace detail = gucc::chwd::detail;

static constexpr auto LIST_ALL_OUTPUT = R"(> All PCI profiles:

╭─────────────────────────┬──────────╮
│ Name                    ┆ Priority │
╞═════════════════════════╪══════════╡
│ nouveau                 ┆ 18       │
├╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌╌┤
│ nvidia-dkms-470xx.prime ┆ 15       │
├╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌╌┤
│ handheld.steam-deck     ┆ 2        │
╰─────────────────────────┴──────────╯

> All USB profiles:

╭────────┬──────────╮
│ Name   ┆ Priority │
╞════════╪══════════╡
│ fprint ┆ 5        │
╰────────┴──────────╯
)"sv;

static constexpr auto LIST_DETAIL_OUTPUT = R"(> PCI Device:  (0300:10de:2783)
  VGA compatible controller NVIDIA Corporation AD104 [GeForce RTX 4070 SUPER]

  > AVAILABLE:

╭───────────┬───────────────────────────────────────────────╮
│ Name      ┆ nvidia-open-dkms                              │
├╌╌╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┤
│ Desc      ┆ Open source NVIDIA drivers for Linux (Latest) │
├╌╌╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┤
│ Priority  ┆ 10                                            │
╰───────────┴───────────────────────────────────────────────╯

╭───────────┬──────────────────╮
│ Name      ┆ fallback         │
├╌╌╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┤
│ Desc      ┆ Fallback profile │
╰───────────┴──────────────────╯
)"sv;

TEST_CASE("chwd table parsing")
{
    SECTION("list-all yields every profile row, skipping headers and borders")
    {
        const auto names = detail::parse_profile_table(LIST_ALL_OUTPUT);
        REQUIRE_EQ(names, std::vector<std::string>{"nouveau", "nvidia-dkms-470xx.prime", "handheld.steam-deck", "fprint"});
    }
    SECTION("list -d yields the Name value of every detail table")
    {
        const auto names = detail::parse_profile_details(LIST_DETAIL_OUTPUT);
        REQUIRE_EQ(names, std::vector<std::string>{"nvidia-open-dkms", "fallback"});
    }
    SECTION("empty output")
    {
        REQUIRE(detail::parse_profile_table(""sv).empty());
        REQUIRE(detail::parse_profile_details(""sv).empty());
    }
    SECTION("lone border not a row")
    {
        REQUIRE(detail::parse_profile_table("│\n  │  \n"sv).empty());
        REQUIRE(detail::parse_profile_details("│"sv).empty());
    }
}
