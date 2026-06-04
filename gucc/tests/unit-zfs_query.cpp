#include "doctest_compatibility.h"

#include "gucc/zfs_query.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace detail = gucc::fs::zfs_query::detail;

TEST_CASE("parse_zfs_size")
{
    SECTION("empty input returns 0")
    {
        REQUIRE_EQ(detail::parse_zfs_size(""sv), 0u);
    }
    SECTION("\"-\" placeholder returns 0")
    {
        REQUIRE_EQ(detail::parse_zfs_size("-"sv), 0u);
    }
    SECTION("raw byte count from `-Hp` output")
    {
        REQUIRE_EQ(detail::parse_zfs_size("1234567890"sv), 1234567890ULL);
    }
    SECTION("suffix multipliers (legacy human-readable mode)")
    {
        REQUIRE_EQ(detail::parse_zfs_size("1K"sv), 1024ULL);
        REQUIRE_EQ(detail::parse_zfs_size("1M"sv), 1024ULL * 1024);
        REQUIRE_EQ(detail::parse_zfs_size("1G"sv), 1024ULL * 1024 * 1024);
        REQUIRE_EQ(detail::parse_zfs_size("1T"sv), 1024ULL * 1024 * 1024 * 1024);
    }
    SECTION("fractional values multiply correctly")
    {
        REQUIRE_EQ(detail::parse_zfs_size("1.5G"sv),
            static_cast<std::uint64_t>(1.5 * 1024 * 1024 * 1024));
    }
    SECTION("malformed input returns 0")
    {
        REQUIRE_EQ(detail::parse_zfs_size("garbage"sv), 0u);
    }
}

TEST_CASE("split_tsv preserves empty cells")
{
    SECTION("basic split")
    {
        const auto fields = detail::split_tsv("a\tb\tc"sv);
        REQUIRE_EQ(fields.size(), 3u);
        REQUIRE_EQ(fields[0], "a");
        REQUIRE_EQ(fields[1], "b");
        REQUIRE_EQ(fields[2], "c");
    }
    SECTION("empty middle cell keeps the column index stable")
    {
        const auto fields = detail::split_tsv("a\t\tc"sv);
        REQUIRE_EQ(fields.size(), 3u);
        REQUIRE_EQ(fields[0], "a");
        REQUIRE_EQ(fields[1], "");
        REQUIRE_EQ(fields[2], "c");
    }
    SECTION("trailing empty cell")
    {
        const auto fields = detail::split_tsv("a\tb\t"sv);
        REQUIRE_EQ(fields.size(), 3u);
        REQUIRE_EQ(fields[2], "");
    }
    SECTION("no tab returns single field")
    {
        const auto fields = detail::split_tsv("solo"sv);
        REQUIRE_EQ(fields.size(), 1u);
        REQUIRE_EQ(fields[0], "solo");
    }
    SECTION("empty input still yields a single empty field")
    {
        const auto fields = detail::split_tsv(""sv);
        REQUIRE_EQ(fields.size(), 1u);
        REQUIRE_EQ(fields[0], "");
    }
}

TEST_CASE("parse_dataset_fields")
{
    SECTION("too few fields returns nullopt")
    {
        std::vector<std::string> fields{"tank", "/tank", "0"};
        REQUIRE_FALSE(detail::parse_dataset_fields(std::move(fields)).has_value());
    }
    SECTION("complete row, encryption off, no recordsize")
    {
        std::vector<std::string> fields{
            "tank/home"s, "/tank/home"s, "1048576"s, "10485760"s, "524288"s,
            "lz4"s, "off"s, "131072"s, "filesystem"s, "yes"s,
        };
        const auto ds = detail::parse_dataset_fields(std::move(fields));
        REQUIRE(ds.has_value());
        REQUIRE_EQ(ds->name, "tank/home");
        REQUIRE_EQ(ds->mountpoint, "/tank/home");
        REQUIRE_EQ(ds->used, 1048576u);
        REQUIRE_EQ(ds->available, 10485760u);
        REQUIRE_EQ(ds->referenced, 524288u);
        REQUIRE(ds->compression.has_value());
        REQUIRE_EQ(*ds->compression, "lz4");
        REQUIRE_FALSE(ds->encryption);
        REQUIRE(ds->recordsize.has_value());
        REQUIRE_EQ(*ds->recordsize, 131072u);
        REQUIRE_EQ(ds->type, "filesystem");
        REQUIRE(ds->mounted);
    }
    SECTION("encryption on flag")
    {
        std::vector<std::string> fields{
            "tank"s, "/tank"s, "0"s, "0"s, "0"s,
            "off"s, "aes-256-gcm"s, "-"s, "filesystem"s, "no"s,
        };
        const auto ds = detail::parse_dataset_fields(std::move(fields));
        REQUIRE(ds.has_value());
        REQUIRE(ds->encryption);
        REQUIRE_FALSE(ds->compression.has_value());
        REQUIRE_FALSE(ds->recordsize.has_value());
        REQUIRE_FALSE(ds->mounted);
    }
    SECTION("9-column row (no mounted column) parses; mounted stays false")
    {
        std::vector<std::string> fields{
            "tank"s, "/tank"s, "0"s, "0"s, "0"s,
            "-"s, "-"s, "-"s, "filesystem"s,
        };
        const auto ds = detail::parse_dataset_fields(std::move(fields));
        REQUIRE(ds.has_value());
        REQUIRE_FALSE(ds->compression.has_value());
        REQUIRE_FALSE(ds->encryption);
        REQUIRE_FALSE(ds->mounted);
    }
}

TEST_CASE("zfs_pool_health round-trip")
{
    using gucc::fs::ZfsPoolHealth;
    using gucc::fs::string_to_zfs_pool_health;
    using gucc::fs::zfs_pool_health_to_string;

    SECTION("known values")
    {
        REQUIRE(string_to_zfs_pool_health("ONLINE"sv) == ZfsPoolHealth::Online);
        REQUIRE(string_to_zfs_pool_health("DEGRADED"sv) == ZfsPoolHealth::Degraded);
        REQUIRE(string_to_zfs_pool_health("FAULTED"sv) == ZfsPoolHealth::Faulted);
        REQUIRE(string_to_zfs_pool_health("OFFLINE"sv) == ZfsPoolHealth::Offline);
        REQUIRE(string_to_zfs_pool_health("UNAVAIL"sv) == ZfsPoolHealth::Unavailable);
        REQUIRE(string_to_zfs_pool_health("SUSPENDED"sv) == ZfsPoolHealth::Suspended);
    }
    SECTION("unknown string falls back to Unknown")
    {
        REQUIRE(string_to_zfs_pool_health("???"sv) == ZfsPoolHealth::Unknown);
    }
    SECTION("symmetric for known values")
    {
        for (const auto v : {ZfsPoolHealth::Online, ZfsPoolHealth::Degraded,
                 ZfsPoolHealth::Faulted, ZfsPoolHealth::Offline,
                 ZfsPoolHealth::Unavailable, ZfsPoolHealth::Suspended}) {
            REQUIRE(string_to_zfs_pool_health(zfs_pool_health_to_string(v)) == v);
        }
    }
}
