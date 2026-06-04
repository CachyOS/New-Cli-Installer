#include "doctest_compatibility.h"

#include "gucc/btrfs_query.hpp"

#include <string_view>

using namespace std::string_view_literals;

namespace detail = gucc::fs::btrfs_query::detail;

TEST_CASE("parse_subvolume_list")
{
    SECTION("empty input yields empty vector")
    {
        const auto subvols = detail::parse_subvolume_list(""sv, "@"sv);
        REQUIRE(subvols.empty());
    }
    SECTION("single root subvolume with @ prefix")
    {
        static constexpr auto input =
            "ID 256 gen 14 parent 5 top level 5 parent_uuid - uuid aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee path @"sv;

        const auto subvols = detail::parse_subvolume_list(input, "@"sv);
        REQUIRE_EQ(subvols.size(), 1u);

        const auto& s = subvols.front();
        REQUIRE_EQ(s.id, 256u);
        REQUIRE_EQ(s.gen, 14u);
        REQUIRE_EQ(s.parent_id, 5u);
        REQUIRE_EQ(s.top_level, 5u);
        REQUIRE_EQ(s.path, "@");
        REQUIRE(s.uuid.has_value());
        REQUIRE_EQ(*s.uuid, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
        REQUIRE_FALSE(s.parent_uuid.has_value());
    }
    SECTION("snapshot has parent_uuid populated")
    {
        static constexpr auto input =
            "ID 270 gen 99 parent 5 top level 5 parent_uuid 11111111-2222-3333-4444-555555555555 uuid 66666666-7777-8888-9999-aaaaaaaaaaaa path @/.snapshots/1"sv;

        const auto subvols = detail::parse_subvolume_list(input, ""sv);
        REQUIRE_EQ(subvols.size(), 1u);

        const auto& s = subvols.front();
        REQUIRE(s.parent_uuid.has_value());
        REQUIRE_EQ(*s.parent_uuid, "11111111-2222-3333-4444-555555555555");
        REQUIRE(s.uuid.has_value());
        REQUIRE_EQ(*s.uuid, "66666666-7777-8888-9999-aaaaaaaaaaaa");
        REQUIRE_EQ(s.path, "@/.snapshots/1");
    }
    SECTION("multiple subvolumes, filter_prefix=\"@\" keeps only the matches")
    {
        static constexpr auto input =
            "ID 256 gen 14 parent 5 top level 5 parent_uuid - uuid aaaa path @\n"
            "ID 257 gen 15 parent 5 top level 5 parent_uuid - uuid bbbb path @home\n"
            "ID 258 gen 16 parent 5 top level 5 parent_uuid - uuid cccc path @log\n"
            "ID 999 gen 50 parent 5 top level 5 parent_uuid - uuid dddd path var/lib/docker/btrfs/subvolumes/abc\n"sv;

        const auto subvols = detail::parse_subvolume_list(input, "@"sv);
        REQUIRE_EQ(subvols.size(), 3u);
        REQUIRE_EQ(subvols[0].path, "@");
        REQUIRE_EQ(subvols[1].path, "@home");
        REQUIRE_EQ(subvols[2].path, "@log");
    }
    SECTION("empty filter_prefix admits every subvolume")
    {
        static constexpr auto input =
            "ID 256 gen 14 parent 5 top level 5 parent_uuid - uuid aaaa path @\n"
            "ID 999 gen 50 parent 5 top level 5 parent_uuid - uuid dddd path var/lib/docker/btrfs/subvolumes/abc\n"sv;

        const auto subvols = detail::parse_subvolume_list(input, ""sv);
        REQUIRE_EQ(subvols.size(), 2u);
        REQUIRE_EQ(subvols[0].path, "@");
        REQUIRE_EQ(subvols[1].path, "var/lib/docker/btrfs/subvolumes/abc");
    }
    SECTION("lines without a path field are skipped")
    {
        static constexpr auto input =
            "ID 256 gen 14 parent 5 top level 5 parent_uuid - uuid aaaa\n"
            "ID 257 gen 15 parent 5 top level 5 parent_uuid - uuid bbbb path @home\n"sv;

        const auto subvols = detail::parse_subvolume_list(input, "@"sv);
        REQUIRE_EQ(subvols.size(), 1u);
        REQUIRE_EQ(subvols[0].path, "@home");
    }
    SECTION("parent_uuid \"-\" placeholder leaves the optional empty")
    {
        static constexpr auto input =
            "ID 256 gen 14 parent 5 top level 5 parent_uuid - uuid aaaa path @"sv;

        const auto subvols = detail::parse_subvolume_list(input, ""sv);
        REQUIRE_EQ(subvols.size(), 1u);
        REQUIRE_FALSE(subvols.front().parent_uuid.has_value());
    }
}
