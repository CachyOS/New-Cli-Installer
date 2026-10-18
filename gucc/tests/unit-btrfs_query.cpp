#include "doctest_compatibility.h"

#include "gucc/btrfs_query.hpp"

#include <string>
#include <string_view>

using namespace std::string_literals;
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
        static constexpr auto input = "ID 256 gen 14 parent 5 top level 5 parent_uuid - uuid aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee path @"sv;

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
        static constexpr auto input = "ID 270 gen 99 parent 5 top level 5 parent_uuid 11111111-2222-3333-4444-555555555555 uuid 66666666-7777-8888-9999-aaaaaaaaaaaa path @/.snapshots/1"sv;

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
        static constexpr auto input = "ID 256 gen 14 parent 5 top level 5 parent_uuid - uuid aaaa path @\n"
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
        static constexpr auto input = "ID 256 gen 14 parent 5 top level 5 parent_uuid - uuid aaaa path @\n"
                                      "ID 999 gen 50 parent 5 top level 5 parent_uuid - uuid dddd path var/lib/docker/btrfs/subvolumes/abc\n"sv;

        const auto subvols = detail::parse_subvolume_list(input, ""sv);
        REQUIRE_EQ(subvols.size(), 2u);
        REQUIRE_EQ(subvols[0].path, "@");
        REQUIRE_EQ(subvols[1].path, "var/lib/docker/btrfs/subvolumes/abc");
    }
    SECTION("lines without a path field are skipped")
    {
        static constexpr auto input = "ID 256 gen 14 parent 5 top level 5 parent_uuid - uuid aaaa\n"
                                      "ID 257 gen 15 parent 5 top level 5 parent_uuid - uuid bbbb path @home\n"sv;

        const auto subvols = detail::parse_subvolume_list(input, "@"sv);
        REQUIRE_EQ(subvols.size(), 1u);
        REQUIRE_EQ(subvols[0].path, "@home");
    }
    SECTION("parent_uuid \"-\" placeholder leaves the optional empty")
    {
        static constexpr auto input = "ID 256 gen 14 parent 5 top level 5 parent_uuid - uuid aaaa path @"sv;

        const auto subvols = detail::parse_subvolume_list(input, ""sv);
        REQUIRE_EQ(subvols.size(), 1u);
        REQUIRE_FALSE(subvols.front().parent_uuid.has_value());
    }
}

TEST_CASE("parse_btrfs_usage")
{
    static constexpr auto kUsage = "Overall:\n"
                                   "    Device size:\t\t        21474836480\n"
                                   "    Device allocated:\t\t         2172649472\n"
                                   "    Device unallocated:\t\t        19302187008\n"
                                   "    Device missing:\t\t                  0\n"
                                   "    Device slack:\t\t                  0\n"
                                   "    Used:\t\t\t          394264576\n"
                                   "    Free (estimated):\t\t        20271456256\t(min: 10620362752)\n"
                                   "    Free (statfs, df):\t\t        20270407680\n"
                                   "    Data ratio:\t\t\t               1.00\n"
                                   "    Metadata ratio:\t\t               2.00\n"
                                   "    Global reserve:\t\t            5767168\t(used: 0)\n"
                                   "    Multiple profiles:\t\t                 no\n"
                                   "\n"
                                   "Data,single: Size:1107296256, Used:137871360 (12.45%)\n"
                                   "   /dev/vda2\t1107296256\n"
                                   "\n"
                                   "Metadata,DUP: Size:524288000, Used:128155648 (24.44%)\n"
                                   "   /dev/vda2\t1048576000\n"
                                   "\n"
                                   "System,DUP: Size:8388608, Used:16384 (0.20%)\n"
                                   "   /dev/vda2\t  16777216\n"
                                   "\n"
                                   "Unallocated:\n"
                                   "   /dev/vda2\t19302187008\n"sv;

    SECTION("sizes and profiles")
    {
        const auto parsed = detail::parse_btrfs_usage(kUsage);
        REQUIRE(parsed.has_value());
        const auto& info = *parsed;
        CHECK_EQ(info.total_size, 21474836480u);
        CHECK_EQ(info.used, 394264576u);
        CHECK_EQ(info.free, 20271456256u);
        CHECK_EQ(info.data_profile, "single"sv);
        CHECK_EQ(info.metadata_profile, "DUP"sv);
        CHECK(info.device.empty());
        CHECK_FALSE(info.uuid.has_value());
    }
    SECTION("RAID1 profiles")
    {
        const auto info = detail::parse_btrfs_usage("    Device size:\t\t 1073741824\nData,RAID1: Size:1, Used:1\nMetadata,RAID1C3: Size:1, Used:1\n"sv);
        REQUIRE(info.has_value());
        CHECK_EQ(info->data_profile, "RAID1"sv);
        CHECK_EQ(info->metadata_profile, "RAID1C3"sv);
    }
    SECTION("output without a device size is refused")
    {
        CHECK_FALSE(detail::parse_btrfs_usage(""sv).has_value());
        CHECK_FALSE(detail::parse_btrfs_usage("ERROR: not a btrfs filesystem: /\n"sv).has_value());
        CHECK_FALSE(detail::parse_btrfs_usage("    Device size:\tgarbage\nData,single: Size:1\n"sv).has_value());
        CHECK_FALSE(detail::parse_btrfs_usage("    Device size:\t\t 0\n"sv).has_value());
    }
    SECTION("real -b output, space-aligned")
    {
        static constexpr auto kUsageBytes = "Overall:\n"
                                            "    Device size:         1023136370688\n"
                                            "    Device allocated:         918070730752\n"
                                            "    Device unallocated:          105054896128\n"
                                            "    Device missing:             0\n"
                                            "    Device slack:             0\n"
                                            "    Used:             782092066816\n"
                                            "    Free (estimated):         211065430016    (min: 158537977856)\n"
                                            "    Free (statfs, df):         211065430016\n"
                                            "    Data ratio:                  1.00\n"
                                            "    Metadata ratio:              2.00\n"
                                            "    Global reserve:         536870912    (used: 0)\n"
                                            "    Multiple profiles:                no\n"
                                            "\n"
                                            "Data,single: Size:870815354880, Used:764804825088 (87.83%)\n"
                                            "   /dev/nvme0n1p2     870815354880\n"
                                            "\n"
                                            "Metadata,DUP: Size:23622320128, Used:8643620864 (36.60%)\n"
                                            "   /dev/nvme0n1p2      47244640256\n"
                                            "\n"
                                            "System,DUP: Size:8388608, Used:114688 (1.37%)\n"
                                            "   /dev/nvme0n1p2      16777216\n"
                                            "\n"
                                            "Unallocated:\n"
                                            "   /dev/nvme0n1p2      105054896128\n"sv;
        const auto info                   = detail::parse_btrfs_usage(kUsageBytes);
        REQUIRE(info.has_value());
        CHECK_EQ(info->total_size, 1023136370688u);
        CHECK_EQ(info->used, 782092066816u);
        CHECK_EQ(info->free, 211065430016u);
        CHECK_EQ(info->data_profile, "single"sv);
        CHECK_EQ(info->metadata_profile, "DUP"sv);
    }
    SECTION("-h units are refused, not misread as bytes")
    {
        static constexpr auto kUsageHuman = "Overall:\n"
                                            "    Device size:         952.87GiB\n"
                                            "    Device allocated:         855.02GiB\n"
                                            "    Device unallocated:          97.84GiB\n"
                                            "    Device missing:             0.00B\n"
                                            "    Device slack:             0.00B\n"
                                            "    Used:             728.38GiB\n"
                                            "    Free (estimated):         196.57GiB    (min: 147.65GiB)\n"
                                            "    Free (statfs, df):         196.57GiB\n"
                                            "    Data ratio:                  1.00\n"
                                            "    Metadata ratio:              2.00\n"
                                            "    Global reserve:         512.00MiB    (used: 0.00B)\n"
                                            "    Multiple profiles:                no\n"
                                            "\n"
                                            "Data,single: Size:811.01GiB, Used:712.28GiB (87.83%)\n"
                                            "   /dev/nvme0n1p2     811.01GiB\n"
                                            "\n"
                                            "Metadata,DUP: Size:22.00GiB, Used:8.05GiB (36.60%)\n"
                                            "   /dev/nvme0n1p2      44.00GiB\n"
                                            "\n"
                                            "System,DUP: Size:8.00MiB, Used:112.00KiB (1.37%)\n"
                                            "   /dev/nvme0n1p2      16.00MiB\n"
                                            "\n"
                                            "Unallocated:\n"
                                            "   /dev/nvme0n1p2      97.84GiB\n"sv;
        CHECK_FALSE(detail::parse_btrfs_usage(kUsageHuman).has_value());
    }
    SECTION("indentation and key/value spacing do not matter")
    {
        const auto info = detail::parse_btrfs_usage("Device size: 4096\n\t\tUsed :\t1024\n  Free (estimated):2048\t(min: 1)\n\tData,RAID10 : Size:1\n Metadata,DUP: Size:1\n"sv);
        REQUIRE(info.has_value());
        CHECK_EQ(info->total_size, 4096u);
        CHECK_EQ(info->used, 1024u);
        CHECK_EQ(info->free, 2048u);
        CHECK_EQ(info->data_profile, "RAID10"sv);
        CHECK_EQ(info->metadata_profile, "DUP"sv);
    }
    SECTION("keys match exactly")
    {
        const auto info = detail::parse_btrfs_usage("    Device size:\t1024\nData,single: Size:1, Used:999\n    Free (statfs, df):\t77\n"sv);
        REQUIRE(info.has_value());
        CHECK_EQ(info->used, 0u);
        CHECK_EQ(info->free, 0u);
    }
    SECTION("profile line without a colon is ignored")
    {
        const auto info = detail::parse_btrfs_usage("    Device size:\t\t 1024\nData,\n"sv);
        REQUIRE(info.has_value());
        CHECK(info->data_profile.empty());
    }
}

TEST_CASE("parse_btrfs_show")
{
    SECTION("label and uuid on the same line")
    {
        static constexpr auto kShow = "Label: 'cachyos'  uuid: 3f8a2c1e-5b4d-4e6f-9a7b-1c2d3e4f5a6b\n"
                                      "\tTotal devices 1 FS bytes used 262.08MiB\n"
                                      "\tdevid    1 size 20.00GiB used 2.02GiB path /dev/vda2\n"
                                      "\n"sv;
        const auto show             = detail::parse_btrfs_show(kShow);
        CHECK_EQ(show.label, "cachyos"s);
        CHECK_EQ(show.uuid, "3f8a2c1e-5b4d-4e6f-9a7b-1c2d3e4f5a6b"s);
    }
    SECTION("no label")
    {
        const auto show = detail::parse_btrfs_show("Label: none  uuid: 11111111-2222-3333-4444-555555555555\n"sv);
        CHECK_FALSE(show.label.has_value());
        CHECK_EQ(show.uuid, "11111111-2222-3333-4444-555555555555"s);
    }
    SECTION("label with quotes and spaces")
    {
        const auto show = detail::parse_btrfs_show("Label: 'it's my uuid: disk'  uuid: aaaa-bbbb\n"sv);
        CHECK_EQ(show.label, "it's my uuid: disk"s);
        CHECK_EQ(show.uuid, "aaaa-bbbb"s);
    }
    SECTION("any whitespace between keys and values")
    {
        const auto quoted = detail::parse_btrfs_show("Label:'cachyos'\tuuid:\t3f8a\n"sv);
        CHECK_EQ(quoted.label, "cachyos"s);
        CHECK_EQ(quoted.uuid, "3f8a"s);
        const auto spaced = detail::parse_btrfs_show("  Label:    'my label'      uuid:   3f8a   \n"sv);
        CHECK_EQ(spaced.label, "my label"s);
        CHECK_EQ(spaced.uuid, "3f8a"s);
        const auto none = detail::parse_btrfs_show("Label:\tnone\t\tuuid:\t1111\n"sv);
        CHECK_FALSE(none.label.has_value());
        CHECK_EQ(none.uuid, "1111"s);
    }
    SECTION("keys are whole tokens")
    {
        const auto show = detail::parse_btrfs_show("Label: 'a'  fsuuid: nope  uuid: 2222\n"sv);
        CHECK_EQ(show.label, "a"s);
        CHECK_EQ(show.uuid, "2222"s);
        CHECK_FALSE(detail::parse_btrfs_show("Label: 'a'  fsuuid: nope\n"sv).uuid.has_value());
    }
    SECTION("first filesystem wins")
    {
        const auto show = detail::parse_btrfs_show("Label: 'one'  uuid: 1111\n\tdevid 1\n\nLabel: 'two'  uuid: 2222\n"sv);
        CHECK_EQ(show.label, "one"s);
        CHECK_EQ(show.uuid, "1111"s);
    }
    SECTION("empty and error output")
    {
        CHECK_FALSE(detail::parse_btrfs_show(""sv).uuid.has_value());
        const auto show = detail::parse_btrfs_show("ERROR: not a valid btrfs filesystem: /dev/sda1\nLabel: 'unterminated\n"sv);
        CHECK_FALSE(show.uuid.has_value());
        CHECK_FALSE(show.label.has_value());
    }
}

TEST_CASE("parse_subvolume_paths")
{
    static constexpr auto kList = "ID 256 gen 1120 top level 5 path @\n"
                                  "ID 257 gen 1119 top level 5 path @home\n"
                                  "ID 258 gen 1100 top level 5 path @var cache\n"
                                  "ID 260 gen 900 top level 256 path var/lib/docker/btrfs/subvolumes/abc\n"
                                  "ID 261 gen 900 top level 5 path \n"
                                  "garbage line\n"sv;

    SECTION("default @ prefix")
    {
        const auto subvols = detail::parse_subvolume_paths(kList, "@"sv);
        REQUIRE_EQ(subvols.size(), 3u);
        CHECK_EQ(subvols[0].subvolume, "@"sv);
        CHECK_EQ(subvols[0].mountpoint, "/@"sv);
        CHECK_EQ(subvols[1].subvolume, "@home"sv);
        CHECK_EQ(subvols[2].subvolume, "@var cache"sv);
        CHECK_EQ(subvols[2].mountpoint, "/@var cache"sv);
    }
    SECTION("empty prefix keeps everything with a path")
    {
        CHECK_EQ(detail::parse_subvolume_paths(kList, ""sv).size(), 4u);
    }
    SECTION("empty output")
    {
        CHECK(detail::parse_subvolume_paths(""sv, ""sv).empty());
    }
}
