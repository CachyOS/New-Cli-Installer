#include "doctest_compatibility.h"

#include "gucc/zfs_query.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace detail = gucc::fs::zfs_query::detail;

TEST_CASE("zfs_query")
{
    SECTION("parse_zfs_size")
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
        SECTION("UB conversions should return 0")
        {
            REQUIRE_EQ(detail::parse_zfs_size("-5"sv), 0u);
            REQUIRE_EQ(detail::parse_zfs_size("nan"sv), 0u);
            REQUIRE_EQ(detail::parse_zfs_size("inf"sv), 0u);
            REQUIRE_EQ(detail::parse_zfs_size("1e30"sv), 0u);
            REQUIRE_EQ(detail::parse_zfs_size("16777216T"sv), 0u);
            REQUIRE_EQ(detail::parse_zfs_size("16777215T"sv), 16777215ULL << 40U);
        }
    }
    SECTION("split_tsv preserves empty cells")
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
    SECTION("parse_dataset_fields")
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
    SECTION("zfs_pool_health round-trip")
    {
        using gucc::fs::ZfsPoolHealth;
        using gucc::fs::string_to_zfs_pool_health;
        using gucc::fs::zfs_pool_health_to_string;
        using gucc::fs::ZfsPoolHealth;

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
}

TEST_CASE("zfs_query zpool/zfs output parsing")
{
    using gucc::fs::ZfsDatasetInfo;

    SECTION("parse_zpool_status_devices keeps only absolute vdev paths")
    {
        constexpr auto status = "  pool: zpcachyos\n"
                                " state: ONLINE\n"
                                "config:\n"
                                "\n"
                                "\tNAME             STATE     READ WRITE CKSUM\n"
                                "\tzpcachyos        ONLINE       0     0     0\n"
                                "\t  /dev/vda2      ONLINE       0     0     0\n"
                                "\t  mirror-0       ONLINE       0     0     0\n"
                                "\t    /dev/sdb1    ONLINE       0     0     0\n"
                                "\n"
                                "errors: No known data errors\n"sv;
        REQUIRE_EQ(detail::parse_zpool_status_devices(status), std::vector<std::string>{"/dev/vda2", "/dev/sdb1"});
        REQUIRE(detail::parse_zpool_status_devices(""sv).empty());
    }
    SECTION("parse_zpool_status_pools groups vdevs by pool")
    {
        constexpr auto status = "  pool: tank\n"
                                " state: ONLINE\n"
                                "config:\n"
                                "\n"
                                "\tNAME        STATE     READ WRITE CKSUM\n"
                                "\ttank        ONLINE       0     0     0\n"
                                "\t  /dev/vda2  ONLINE       0     0     0\n"
                                "\n"
                                "errors: No known data errors\n"
                                "\n"
                                "  pool: zpcachyos\n"
                                " state: ONLINE\n"
                                "config:\n"
                                "\t  mirror-0     ONLINE       0     0     0\n"
                                "\t    /dev/sdb1  ONLINE       0     0     0\n"
                                "\t    /dev/sdc1  ONLINE       0     0     0\n"sv;
        const auto pools      = detail::parse_zpool_status_pools(status);
        REQUIRE_EQ(pools.size(), 2);
        REQUIRE_EQ(pools.at("tank"), std::vector<std::string>{"/dev/vda2"});
        REQUIRE_EQ(pools.at("zpcachyos"), std::vector<std::string>{"/dev/sdb1", "/dev/sdc1"});
        REQUIRE(detail::parse_zpool_status_pools("no pools available\n"sv).empty());
        REQUIRE(detail::parse_zpool_status_pools(""sv).empty());
    }
    SECTION("parse_zpool_get_values maps each pool to the VALUE column")
    {
        const auto values = detail::parse_zpool_get_values("zpcachyos\tbootfs\tzpcachyos/ROOT/cos-root\tlocal\n"
                                                           "tank\tbootfs\t-\tdefault\n"
                                                           "malformed\n"sv);
        REQUIRE_EQ(values.size(), 2);
        REQUIRE_EQ(values.at("zpcachyos"), "zpcachyos/ROOT/cos-root"s);
        REQUIRE_EQ(values.at("tank"), "-"s);
        REQUIRE(detail::parse_zpool_get_values(""sv).empty());
    }
    SECTION("child dataset names")
    {
        const auto make_ds = [](std::string name, std::string mountpoint, std::string type) {
            ZfsDatasetInfo ds{};
            ds.name       = std::move(name);
            ds.mountpoint = std::move(mountpoint);
            ds.type       = std::move(type);
            return ds;
        };
        const std::vector<ZfsDatasetInfo> datasets{
            make_ds("zpcachyos", "none", "filesystem"),
            make_ds("zpcachyos/ROOT/cos/root", "/", "filesystem"),
            make_ds("zpcachyos/legacy", "legacy", "filesystem"),
            make_ds("zpcachyos/swap", "-", "volume"),
        };
        REQUIRE_EQ(detail::child_dataset_names(datasets), std::vector<std::string>{"zpcachyos/ROOT/cos/root", "zpcachyos/legacy", "zpcachyos/swap"});
        REQUIRE_EQ(detail::child_dataset_names(datasets, true), std::vector<std::string>{"zpcachyos/legacy"});
    }
}

TEST_CASE("parse importable zpools")
{
    SECTION("multiple pools")
    {
        static constexpr auto output = R"(   pool: zpcachyos
     id: 1234567890
  state: ONLINE
 action: The pool can be imported using its name or numeric identifier.
 config:

        zpcachyos   ONLINE
          vda2      ONLINE

   pool: tank
     id: 987
  state: ONLINE
)"sv;
        REQUIRE_EQ(gucc::fs::zfs_query::detail::parse_importable_zpools(output), std::vector{"zpcachyos"s, "tank"s});
    }
    SECTION("no pools")
    {
        REQUIRE(gucc::fs::zfs_query::detail::parse_importable_zpools("no pools available to import\n"sv).empty());
        REQUIRE(gucc::fs::zfs_query::detail::parse_importable_zpools(""sv).empty());
    }
    SECTION("empty pool name is skipped")
    {
        REQUIRE(gucc::fs::zfs_query::detail::parse_importable_zpools("   pool:   \n"sv).empty());
    }
}

TEST_CASE("zpool list parsing")
{
    SECTION("percentages")
    {
        CHECK_EQ(detail::parse_percentage("12"sv), 12u);
        CHECK_EQ(detail::parse_percentage("0"sv), 0u);
        CHECK_EQ(detail::parse_percentage("-"sv), 0u);
        CHECK_EQ(detail::parse_percentage(""sv), 0u);
        CHECK_EQ(detail::parse_percentage("abc"sv), 0u);
        CHECK_EQ(detail::parse_percentage("-5"sv), 0u);
    }
    SECTION("string zfs health pool conv")
    {
        using gucc::fs::string_to_zfs_pool_health;
        using gucc::fs::zfs_pool_health_to_string;
        using gucc::fs::ZfsPoolHealth;
        for (const auto health : {ZfsPoolHealth::Online, ZfsPoolHealth::Degraded, ZfsPoolHealth::Faulted,
                 ZfsPoolHealth::Offline, ZfsPoolHealth::Unavailable, ZfsPoolHealth::Suspended}) {
            CHECK_EQ(string_to_zfs_pool_health(zfs_pool_health_to_string(health)), health);
        }
        CHECK_EQ(string_to_zfs_pool_health("online"sv), ZfsPoolHealth::Unknown);
        CHECK_EQ(string_to_zfs_pool_health(""sv), ZfsPoolHealth::Unknown);
        CHECK_EQ(string_to_zfs_pool_health("REMOVED"sv), ZfsPoolHealth::Unknown);
    }
    SECTION("zpool list row parse")
    {
        const auto pool = detail::parse_zpool_list_row("zpcachyos\t107374182400\t2147483648\t105226698752\t3\t2\tONLINE\t/mnt"sv);
        REQUIRE(pool.has_value());
        CHECK_EQ(pool->name, "zpcachyos"sv);
        CHECK_EQ(pool->size, 107374182400u);
        CHECK_EQ(pool->allocated, 2147483648u);
        CHECK_EQ(pool->free, 105226698752u);
        CHECK_EQ(pool->fragmentation, 3u);
        CHECK_EQ(pool->capacity, 2u);
        CHECK_EQ(pool->health, gucc::fs::ZfsPoolHealth::Online);
        CHECK_EQ(pool->altroot, "/mnt"s);
        CHECK(pool->devices.empty());
        CHECK_FALSE(pool->bootfs_set);

        const auto no_altroot = detail::parse_zpool_list_row("tank\t1\t0\t1\t-\t0\tDEGRADED\t-"sv);
        REQUIRE(no_altroot.has_value());
        CHECK_EQ(no_altroot->fragmentation, 0u);
        CHECK_EQ(no_altroot->health, gucc::fs::ZfsPoolHealth::Degraded);
        CHECK_FALSE(no_altroot->altroot.has_value());

        const auto seven_columns = detail::parse_zpool_list_row("tank\t1\t0\t1\t0\t0\tONLINE"sv);
        REQUIRE(seven_columns.has_value());
        CHECK_FALSE(seven_columns->altroot.has_value());

        CHECK_FALSE(detail::parse_zpool_list_row(""sv).has_value());
        CHECK_FALSE(detail::parse_zpool_list_row("tank\t1\t0\t1\t0\t0"sv).has_value());
        CHECK_FALSE(detail::parse_zpool_list_row("no pools available"sv).has_value());
    }
    SECTION("zpool list row parse with params")
    {
        static constexpr auto kList = "zpcachyos\t107374182400\t2147483648\t105226698752\t3\t2\tONLINE\t-\n"
                                      "backup\t53687091200\t0\t53687091200\t0\t0\tONLINE\t-\n"
                                      "short\trow\n"sv;
        const detail::PoolMap<std::vector<std::string>> devices{
            {"zpcachyos", {"/dev/disk/by-id/nvme-part2"}},
        };
        const detail::PoolMap<std::string> bootfs{
            {"zpcachyos", "zpcachyos/ROOT/cos/root"},
            {"backup", "-"},
        };
        const auto pools = detail::parse_zpool_list(kList, devices, bootfs);
        REQUIRE_EQ(pools.size(), 2u);
        CHECK_EQ(pools[0].name, "zpcachyos"sv);
        CHECK_EQ(pools[0].devices, std::vector<std::string>{"/dev/disk/by-id/nvme-part2"});
        CHECK(pools[0].bootfs_set);
        CHECK_EQ(pools[1].name, "backup"sv);
        CHECK(pools[1].devices.empty());
        CHECK_FALSE(pools[1].bootfs_set);

        CHECK(detail::parse_zpool_list(""sv, {}, {}).empty());
    }
}
