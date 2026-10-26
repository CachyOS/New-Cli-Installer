#include "doctest_compatibility.h"

#include "gucc/block_devices.hpp"
#include "gucc/btrfs_query.hpp"
#include "gucc/chwd.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/locale.hpp"
#include "gucc/logger.hpp"
#include "gucc/lvm.hpp"
#include "gucc/testing/command_stub.hpp"
#include "gucc/timezone.hpp"
#include "gucc/zfs.hpp"
#include "gucc/zfs_query.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using gucc::utils::ProcessKind;
using gucc::utils::testing::RecordedCall;
using gucc::utils::testing::ScopedCommandStub;

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

void install_noop_logger() {
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) { });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);
}

auto only_queries(const ScopedCommandStub& stub) -> bool {
    return std::ranges::all_of(stub.calls(), [](ProcessKind kind) { return kind == ProcessKind::Query; }, &RecordedCall::kind);
}

constexpr auto kZfsProbe = "command -v zfs >/dev/null 2>&1 && command -v zpool >/dev/null 2>&1"sv;

constexpr auto kChwdListAll = R"(> All PCI profiles:

╭─────────────────────────┬──────────╮
│ Name                    ┆ Priority │
╞═════════════════════════╪══════════╡
│ nvidia-open-dkms        ┆ 10       │
├╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌╌┤
│ nouveau                 ┆ 18       │
├╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌╌┤
│ fallback                ┆ 0        │
╰─────────────────────────┴──────────╯
)"sv;

constexpr auto kChwdListDetail = R"(> PCI Device:  (0300:10de:2783)

╭───────────┬──────────────────╮
│ Name      ┆ nvidia-open-dkms │
├╌╌╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┤
│ Priority  ┆ 10               │
╰───────────┴──────────────────╯

╭───────────┬──────────────────╮
│ Name      ┆ fallback         │
╰───────────┴──────────────────╯
)"sv;

constexpr auto kBtrfsUsage = "Overall:\n"
                             "    Device size:\t\t 21474836480\n"
                             "    Device allocated:\t\t  2172649472\n"
                             "    Used:\t\t   590413824\n"
                             "    Free (estimated):\t\t 20299087872\t(min: 10648043520)\n"
                             "    Free (statfs, df):\t\t 20298035200\n"
                             "\n"
                             "Data,single: Size:1082130432, Used:545259520 (50.39%)\n"
                             "   /dev/vda2\t1082130432\n"
                             "\n"
                             "Metadata,DUP: Size:536870912, Used:22544384 (4.20%)\n"
                             "   /dev/vda2\t1073741824\n"sv;

constexpr auto kBtrfsShow = "Label: 'cachyos'  uuid: 1c2e3f4a-5b6c-7d8e-9f00-112233445566\n"
                            "\tTotal devices 1 FS bytes used 563.06MiB\n"
                            "\tdevid    1 size 20.00GiB used 2.02GiB path /dev/vda2\n"sv;

constexpr auto kZpoolStatus = "  pool: tank\n"
                              " state: ONLINE\n"
                              "config:\n"
                              "\n"
                              "\tNAME         STATE     READ WRITE CKSUM\n"
                              "\ttank         ONLINE       0     0     0\n"
                              "\t  /dev/vdb1  ONLINE       0     0     0\n"
                              "\n"
                              "errors: No known data errors\n"
                              "\n"
                              "  pool: backup\n"
                              " state: DEGRADED\n"
                              "config:\n"
                              "\n"
                              "\tbackup       DEGRADED     0     0     0\n"
                              "\t  /dev/vdc1  ONLINE       0     0     0\n"sv;

}  // namespace

TEST_CASE("lvm queries")
{
    install_noop_logger();
    ScopedCommandStub stub;

    SECTION("detect_lvm happy path")
    {
        stub.on_prefix("pvs "s, {.output = "  /dev/sda2 \n  /dev/sdb1\n"s})
            .on_prefix("vgs "s, {.output = "  vg0\n"s})
            .on_prefix("lvs "s, {.output = "  vg0-root\n  vg0-swap\n"s});

        const auto info = gucc::lvm::detect_lvm();
        CHECK_EQ(info.physical_volumes, std::vector{"/dev/sda2"s, "/dev/sdb1"s});
        CHECK_EQ(info.volume_groups, std::vector{"vg0"s});
        CHECK_EQ(info.logical_volumes, std::vector{"vg0-root"s, "vg0-swap"s});
        CHECK_EQ(stub.command_lines(),
            std::vector{
                "pvs -o pv_name --noheading 2>/dev/null"s,
                "vgs -o vg_name --noheading 2>/dev/null"s,
                "lvs -o vg_name,lv_name --noheading --separator='-' 2>/dev/null"s,
            });
        CHECK(only_queries(stub));
    }
    SECTION("detect_lvm without lvm2 installed")
    {
        stub.on_prefix(""s, {.exit_code = 127});
        const auto info = gucc::lvm::detect_lvm();
        CHECK(info.physical_volumes.empty());
        CHECK(info.volume_groups.empty());
        CHECK(info.logical_volumes.empty());
    }
    SECTION("detect_lvm drops partial output of a failing command")
    {
        stub.on_prefix("pvs "s, {.output = "  /dev/sda2\n"s, .exit_code = 5})
            .on_prefix("vgs "s, {.output = "  vg0\n"s});
        const auto info = gucc::lvm::detect_lvm();
        CHECK(info.physical_volumes.empty());
        CHECK_EQ(info.volume_groups, std::vector{"vg0"s});
    }
    SECTION("show_volume_groups")
    {
        stub.on_prefix("vgs -o vg_name,vg_size "s, {.output = "  vg0\t<100.00g\n  data\t50.00g\n\n"s});
        const auto groups = gucc::lvm::show_volume_groups();
        REQUIRE_EQ(groups.size(), 2);
        CHECK_EQ(groups[0], std::pair{"vg0"s, "<100.00g"s});
        CHECK_EQ(groups[1], std::pair{"data"s, "50.00g"s});
        CHECK(stub.unmatched().empty());
    }
    SECTION("empty")
    {
        stub.on_prefix("vgs "s, {.exit_code = 5});
        CHECK(gucc::lvm::show_volume_groups().empty());
    }
    SECTION("show_volume_groups drops partial output")
    {
        stub.on_prefix("vgs "s, {.output = "  vg0\t<100.00g\n"s, .exit_code = 5});
        CHECK(gucc::lvm::show_volume_groups().empty());
    }
}

TEST_CASE("btrfs queries")
{
    install_noop_logger();
    ScopedCommandStub stub;

    SECTION("get_btrfs_info parses usage and show")
    {
        stub.on_exact("btrfs filesystem usage -b '/mnt' 2>/dev/null"s, {.output = std::string{kBtrfsUsage}})
            .on_exact("btrfs filesystem show '/mnt' 2>/dev/null"s, {.output = std::string{kBtrfsShow}});

        const auto info = gucc::fs::get_btrfs_info("/mnt"sv);
        REQUIRE(info.has_value());
        CHECK_EQ(info->device, "/mnt"sv);
        CHECK_EQ(info->total_size, 21474836480ULL);
        CHECK_EQ(info->used, 590413824ULL);
        CHECK_EQ(info->free, 20299087872ULL);
        CHECK_EQ(info->data_profile, "single"sv);
        CHECK_EQ(info->metadata_profile, "DUP"sv);
        CHECK_EQ(info->label, "cachyos"s);
        CHECK_EQ(info->uuid, "1c2e3f4a-5b6c-7d8e-9f00-112233445566"s);
        CHECK(stub.unmatched().empty());
        CHECK(only_queries(stub));
    }
    SECTION("get_btrfs_info on a non-btrfs path")
    {
        stub.on_prefix("btrfs "s, {.exit_code = 1});
        CHECK_FALSE(gucc::fs::get_btrfs_info("/mnt"sv).has_value());
    }
    SECTION("get_btrfs_info refuses when only show fails")
    {
        stub.on_exact("btrfs filesystem usage -b '/mnt' 2>/dev/null"s, {.output = std::string{kBtrfsUsage}})
            .on_exact("btrfs filesystem show '/mnt' 2>/dev/null"s, {.exit_code = 1});
        CHECK_FALSE(gucc::fs::get_btrfs_info("/mnt"sv).has_value());
    }
    SECTION("get_btrfs_info refuses unparseable usage")
    {
        stub.on_exact("btrfs filesystem usage -b '/mnt' 2>/dev/null"s, {.output = "Overall:\n"s})
            .on_exact("btrfs filesystem show '/mnt' 2>/dev/null"s, {.output = std::string{kBtrfsShow}});
        CHECK_FALSE(gucc::fs::get_btrfs_info("/mnt"sv).has_value());
        CHECK_EQ(stub.calls().size(), 1);
    }
    SECTION("get_btrfs_info with empty argument runs nothing")
    {
        CHECK_FALSE(gucc::fs::get_btrfs_info(""sv).has_value());
        CHECK(stub.calls().empty());
    }
    SECTION("is_btrfs")
    {
        stub.on_exact("stat -f -c %T '/mnt' 2>/dev/null"s, {.output = "btrfs\n"s})
            .on_exact("stat -f -c %T '/boot' 2>/dev/null"s, {.output = "msdos\n"s});
        CHECK(gucc::fs::is_btrfs("/mnt"sv));
        CHECK_FALSE(gucc::fs::is_btrfs("/boot"sv));
        CHECK_FALSE(gucc::fs::is_btrfs("/missing"sv));
        CHECK_FALSE(gucc::fs::is_btrfs(""sv));
        CHECK_EQ(stub.calls().size(), 3);
    }
    SECTION("list_btrfs_subvolumes filters by prefix")
    {
        stub.on_exact("btrfs subvolume list -o '/mnt' 2>/dev/null"s,
            {.output = "ID 256 gen 10 top level 5 path @\n"
                       "ID 257 gen 10 top level 5 path @home\n"
                       "ID 258 gen 9 top level 256 path var/lib/portables\n"
                       "garbage line\n"s});
        const auto subvols = gucc::fs::list_btrfs_subvolumes("/mnt"sv);
        REQUIRE_EQ(subvols.size(), 2);
        CHECK_EQ(subvols[0].subvolume, "@"sv);
        CHECK_EQ(subvols[0].mountpoint, "/@"sv);
        CHECK_EQ(subvols[1].subvolume, "@home"sv);

        CHECK_EQ(gucc::fs::list_btrfs_subvolumes("/mnt"sv, ""sv).size(), 3);
    }
    SECTION("list_btrfs_subvolumes on failure")
    {
        stub.on_prefix("btrfs subvolume list"s, {.exit_code = 1});
        CHECK(gucc::fs::list_btrfs_subvolumes("/mnt"sv).empty());
    }
    SECTION("list_btrfs_snapshots keeps only subvolumes with a parent")
    {
        stub.on_exact("btrfs subvolume list -puq '/mnt' 2>/dev/null"s,
            {.output = "ID 256 gen 12 parent 5 top level 5 parent_uuid - uuid aaaa-1111 path @\n"
                       "ID 260 gen 20 parent 5 top level 5 parent_uuid aaaa-1111 uuid bbbb-2222 path @snapshots/1\n"s});
        const auto snaps = gucc::fs::list_btrfs_snapshots("/mnt"sv);
        REQUIRE_EQ(snaps.size(), 1);
        CHECK_EQ(snaps[0].id, 260);
        CHECK_EQ(snaps[0].path, "@snapshots/1"sv);
        CHECK_EQ(snaps[0].parent_uuid, "aaaa-1111"s);
        CHECK_EQ(snaps[0].uuid, "bbbb-2222"s);
    }
}

TEST_CASE("zfs queries")
{
    install_noop_logger();
    ScopedCommandStub stub;

    SECTION("list_zfs_pools join")
    {
        stub.on_exact(std::string{kZfsProbe}, {})
            .on_prefix("zpool list -Hp "s,
                {.output = "tank\t10737418240\t1048576\t10736369664\t3\t1\tONLINE\t/mnt\n"
                           "backup\t5368709120\t0\t5368709120\t0\t0\tDEGRADED\t-\n"s})
            .on_exact("zpool status -PL 2>/dev/null"s, {.output = std::string{kZpoolStatus}})
            .on_exact("zpool get -Hp bootfs 2>/dev/null"s,
                {.output = "tank\tbootfs\ttank/ROOT/cos\tlocal\nbackup\tbootfs\t-\tdefault\n"s});

        const auto pools = gucc::fs::list_zfs_pools();
        REQUIRE_EQ(pools.size(), 2);
        CHECK_EQ(pools[0].name, "tank"sv);
        CHECK_EQ(pools[0].size, 10737418240ULL);
        CHECK_EQ(pools[0].fragmentation, 3);
        CHECK_EQ(pools[0].capacity, 1);
        CHECK_EQ(pools[0].health, gucc::fs::ZfsPoolHealth::Online);
        CHECK_EQ(pools[0].altroot, "/mnt"s);
        CHECK_EQ(pools[0].devices, std::vector{"/dev/vdb1"s});
        CHECK(pools[0].bootfs_set);
        CHECK_EQ(pools[1].health, gucc::fs::ZfsPoolHealth::Degraded);
        CHECK_FALSE(pools[1].altroot.has_value());
        CHECK_EQ(pools[1].devices, std::vector{"/dev/vdc1"s});
        CHECK_FALSE(pools[1].bootfs_set);
        CHECK(stub.unmatched().empty());
        CHECK(only_queries(stub));

        const auto tank = gucc::fs::get_zfs_pool_info("tank"sv);
        REQUIRE(tank.has_value());
        CHECK_EQ(tank->name, "tank"sv);
        CHECK_FALSE(gucc::fs::get_zfs_pool_info("nope"sv).has_value());
    }
    SECTION("zfs tools missing")
    {
        stub.on_exact(std::string{kZfsProbe}, {.exit_code = 1});
        CHECK(gucc::fs::list_zfs_pools().empty());
        CHECK(gucc::fs::list_zfs_datasets().empty());
        CHECK_FALSE(gucc::fs::get_zfs_dataset_info("tank/ROOT"sv).has_value());
        CHECK_EQ(stub.command_lines(), std::vector(3, std::string{kZfsProbe}));
    }
    SECTION("no pools")
    {
        stub.on_exact(std::string{kZfsProbe}, {})
            .on_prefix("zpool list"s, {});
        CHECK(gucc::fs::list_zfs_pools().empty());
        CHECK_EQ(stub.count_prefix("zpool status"sv), 0);
    }
    SECTION("noise instead of rows")
    {
        stub.on_exact(std::string{kZfsProbe}, {})
            .on_prefix("zpool list"s, {.output = "no pools available\n"s})
            .on_prefix("zpool "s, {});
        CHECK(gucc::fs::list_zfs_pools().empty());
    }
    SECTION("list_zfs_datasets")
    {
        stub.on_exact(std::string{kZfsProbe}, {})
            .on_prefix("zfs list -Hp -o name,mountpoint,used,avail,refer,compression,encryption,recordsize,type,mounted -r 'zpcachyos'"s,
                {.output = "zpcachyos\tnone\t1000\t2000\t96\toff\toff\t131072\tfilesystem\tno\n"
                           "zpcachyos/ROOT/cos/root\t/\t500\t2000\t500\tzstd\taes-256-gcm\t131072\tfilesystem\tyes\n"
                           "zpcachyos/ROOT/cos/varcache\tlegacy\t1\t2000\t1\tzstd\toff\t-\tfilesystem\tno\n"
                           "short\tline\n"s});

        const auto datasets = gucc::fs::list_zfs_datasets("zpcachyos"sv);
        REQUIRE_EQ(datasets.size(), 3);
        CHECK_FALSE(datasets[0].compression.has_value());
        CHECK_EQ(datasets[1].compression, "zstd"s);
        CHECK(datasets[1].encryption);
        CHECK(datasets[1].mounted);
        CHECK_EQ(datasets[1].recordsize, 131072U);
        CHECK_FALSE(datasets[2].recordsize.has_value());
        CHECK_EQ(datasets[2].mountpoint, "legacy"sv);
    }
    SECTION("zfs_list_datasets legacy uses the structured listing")
    {
        stub.on_exact(std::string{kZfsProbe}, {})
            .on_prefix("zfs list -Hp -o "s,
                {.output = "zp\tnone\t0\t0\t0\toff\toff\t-\tfilesystem\tno\n"
                           "zp/a\tlegacy\t0\t0\t0\toff\toff\t-\tfilesystem\tno\n"
                           "zp/b\t/home\t0\t0\t0\toff\toff\t-\tfilesystem\tno\n"s});
        CHECK_EQ(gucc::fs::zfs_list_datasets("legacy"sv), "zp/a"sv);
        CHECK_EQ(gucc::fs::zfs_list_datasets(), "zp/a\nzp/b"sv);
    }
    SECTION("zpool_list_importable")
    {
        stub.on_exact("zpool import 2>/dev/null"s,
            {.output = "   pool: tank\n"
                       "     id: 1234567890\n"
                       "  state: ONLINE\n"
                       " config:\n"
                       "\n"
                       "\ttank        ONLINE\n"
                       "\t  vdb1      ONLINE\n"
                       "\n"
                       "   pool: backup\n"
                       "     id: 42\n"s});
        CHECK_EQ(gucc::fs::zpool_list_importable(), std::vector{"tank"s, "backup"s});
        CHECK(only_queries(stub));
    }
    SECTION("zpool_list_importable with nothing to import")
    {
        stub.on_exact("zpool import 2>/dev/null"s, {.exit_code = 1});
        CHECK(gucc::fs::zpool_list_importable().empty());
    }
    SECTION("zfs_list_devs lists every vdev")
    {
        stub.on_exact("/usr/bin/zpool status -PL"s, {.output = std::string{kZpoolStatus}});
        CHECK_EQ(gucc::fs::zfs_list_devs(), "/dev/vdb1\n/dev/vdc1\n"sv);
        CHECK_EQ(stub.command_lines(), std::vector{"/usr/bin/zpool status -PL"s});
    }
}

TEST_CASE("block device listing")
{
    install_noop_logger();
    ScopedCommandStub stub;

    SECTION("happy path")
    {
        stub.on_prefix("lsblk -f -o NAME,TYPE,FSTYPE,UUID,PARTUUID,PKNAME,LABEL,SIZE,MOUNTPOINTS,MODEL,PTTYPE,RM,TRAN "s,
            {.output = R"({"blockdevices": [
                {"name":"/dev/vda1", "type":"part", "fstype":"vfat", "uuid":"FB54-6E42", "partuuid":"p-1", "pkname":"/dev/vda", "label":null, "size":536870912, "mountpoints":["/boot"], "model":null},
                {"name":"/dev/vda2", "type":"part", "fstype":"crypto_LUKS", "uuid":"u-2", "partuuid":"p-2", "pkname":"/dev/vda", "label":null, "size":20937965568, "mountpoints":[null], "model":null,
                 "children": [
                    {"name":"/dev/mapper/cryptroot", "type":"crypt", "fstype":"btrfs", "uuid":"u-3", "partuuid":null, "pkname":"/dev/vda2", "label":"root", "size":20921188352, "mountpoints":["/", "/home"], "model":null}
                 ]}
            ]})"s});

        const auto devices = gucc::disk::list_block_devices();
        REQUIRE(devices.has_value());
        REQUIRE_EQ(devices->size(), 3);
        CHECK_EQ((*devices)[0].name, "/dev/vda1"sv);
        CHECK_EQ((*devices)[0].size, 536870912ULL);
        CHECK_EQ((*devices)[0].mountpoints, std::vector{"/boot"s});
        CHECK((*devices)[1].mountpoints.empty());
        CHECK_EQ((*devices)[2].pkname, "/dev/vda2"s);
        CHECK_EQ((*devices)[2].label, "root"s);
        CHECK_EQ((*devices)[2].mountpoints, std::vector{"/"s, "/home"s});
        CHECK(gucc::disk::has_type_in_ancestry(*devices, "/dev/mapper/cryptroot"sv, "part"sv));
        CHECK(only_queries(stub));
    }
    SECTION("empty output")
    {
        stub.on_prefix("lsblk "s, {.exit_code = 1});
        CHECK_FALSE(gucc::disk::list_block_devices().has_value());
    }
    SECTION("stderr noise instead of JSON")
    {
        stub.on_prefix("lsblk "s, {.output = "lsblk: unknown column: MOUNTPOINTS\n"s, .exit_code = 1});
        CHECK_FALSE(gucc::disk::list_block_devices().has_value());
    }
    SECTION("non-JSON output with a zero exit")
    {
        stub.on_prefix("lsblk "s, {.output = "lsblk: something odd\n"s});
        CHECK_FALSE(gucc::disk::list_block_devices().has_value());
    }
    SECTION("no devices is an empty list, not a failure")
    {
        stub.on_prefix("lsblk "s, {.output = "{\"blockdevices\": []}\n"s});
        const auto devices = gucc::disk::list_block_devices();
        REQUIRE(devices.has_value());
        CHECK(devices->empty());
    }
}

TEST_CASE("fs_utils")
{
    install_noop_logger();
    ScopedCommandStub stub;

    SECTION("get_device_info")
    {
        stub.on_exact("lsblk -d -o NAME,TYPE,FSTYPE,UUID,PARTUUID,PKNAME,LABEL,SIZE,MOUNTPOINTS,MODEL,PTTYPE,RM,TRAN -b -p -J '/dev/vda1'"s,
            {.output = R"({"blockdevices": [{"name": "/dev/vda1", "type": "part", "fstype": "vfat", "uuid": "FB54-6E42"}]})"s});
        const auto info = gucc::fs::utils::get_device_info("/dev/vda1"sv);
        CHECK_EQ(info.fstype, "vfat"sv);
        CHECK_EQ(info.uuid, "FB54-6E42"sv);
        CHECK_EQ(gucc::fs::utils::get_device_uuid("/dev/vda1"sv), "FB54-6E42"sv);
        CHECK_EQ(gucc::fs::utils::get_device_fstype("/dev/vda1"sv), "vfat"sv);
        CHECK(only_queries(stub));
    }
    SECTION("unformatted device")
    {
        stub.on_prefix("lsblk -d "s, {.output = R"({"blockdevices": [{"name": "/dev/vdb", "type": "disk", "fstype": null, "uuid": null}]})"s});
        const auto info = gucc::fs::utils::get_device_info("/dev/vdb"sv);
        CHECK(info.fstype.empty());
        CHECK(info.uuid.empty());
    }
    SECTION("stderr noise on a missing device")
    {
        stub.on_prefix("lsblk -d "s, {.output = "lsblk: /dev/nope: not a block device\n"s, .exit_code = 32});
        const auto info = gucc::fs::utils::get_device_info("/dev/nope"sv);
        CHECK(info.fstype.empty());
        CHECK(info.uuid.empty());
    }
    SECTION("get_mountpoint_fs and source")
    {
        stub.on_exact("findmnt -ln -o FSTYPE \"/mnt\""s, {.output = "btrfs\n"s})
            .on_exact("findmnt -ln -o SOURCE \"/mnt\""s, {.output = "/dev/mapper/cryptroot[/@]\n"s})
            .on_prefix("findmnt "s, {.exit_code = 1});
        CHECK_EQ(gucc::fs::utils::get_mountpoint_fs("/mnt"sv), "btrfs"sv);
        CHECK_EQ(gucc::fs::utils::get_mountpoint_source("/mnt"sv), "/dev/mapper/cryptroot[/@]"sv);
        CHECK(gucc::fs::utils::get_mountpoint_fs("/nowhere"sv).empty());
    }
}

TEST_CASE("chwd")
{
    install_noop_logger();
    ScopedCommandStub stub;

    SECTION("available profiles are the detail rows present in list-all")
    {
        stub.on_exact("chwd --list-all"s, {.output = std::string{kChwdListAll}})
            .on_exact("chwd --list -d"s, {.output = std::string{kChwdListDetail}});
        CHECK_EQ(gucc::chwd::get_all_profile_names(), std::vector{"nvidia-open-dkms"s, "nouveau"s, "fallback"s});
        CHECK_EQ(gucc::chwd::get_available_profile_names(), std::vector{"nvidia-open-dkms"s, "fallback"s});
        CHECK(only_queries(stub));
    }
    SECTION("stderr noise is not mistaken for a profile")
    {
        stub.on_exact("chwd --list-all"s, {.output = "Error: failed to read /var/lib/chwd/db\n"s, .exit_code = 1});
        CHECK(gucc::chwd::get_available_profile_names().empty());
        CHECK_EQ(stub.command_lines(), std::vector{"chwd --list-all"s});
    }
}

TEST_CASE("timezone")
{
    install_noop_logger();
    ScopedCommandStub stub;

    SECTION("timedatectl output")
    {
        stub.on_exact("timedatectl list-timezones 2>/dev/null"s, {.output = "Africa/Abidjan\nEurope/Berlin\nUTC\n"s});
        CHECK_EQ(gucc::timezone::get_available_timezones(), std::vector{"Africa/Abidjan"s, "Europe/Berlin"s, "UTC"s});
        CHECK(only_queries(stub));
    }
    SECTION("falls back to zoneinfo when timedatectl fails")
    {
        stub.on_prefix("timedatectl "s, {.exit_code = 1});
        if (!std::filesystem::exists("/usr/share/zoneinfo/Europe/Berlin")) {
            return;
        }
        const auto zones = gucc::timezone::get_available_timezones();
        CHECK(std::ranges::is_sorted(zones));
        CHECK(std::ranges::contains(zones, "Europe/Berlin"s));
    }
}

TEST_CASE("localectl")
{
    install_noop_logger();
    ScopedCommandStub stub;

    SECTION("keymaps")
    {
        stub.on_exact("localectl list-x11-keymap-layouts 2>/dev/null"s, {.output = "de\nus\n"s})
            .on_exact("localectl list-keymaps 2>/dev/null"s, {.output = "de-latin1\nus\n"s});
        CHECK_EQ(gucc::locale::get_x11_keymap_layouts(), std::vector{"de"s, "us"s});
        CHECK_EQ(gucc::locale::get_vconsole_keymap_list(), std::vector{"de-latin1"s, "us"s});
        CHECK(only_queries(stub));
    }
    SECTION("localectl unavailable")
    {
        stub.on_prefix("localectl "s, {.exit_code = 127});
        CHECK(gucc::locale::get_x11_keymap_layouts().empty());
        CHECK(gucc::locale::get_vconsole_keymap_list().empty());
    }
}
