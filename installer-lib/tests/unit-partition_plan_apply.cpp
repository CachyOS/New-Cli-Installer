#include "doctest_compatibility.h"

#include "cachyos/partition_planner.hpp"

// import gucc
#include "gucc/logger.hpp"
#include "gucc/testing/command_stub.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using gucc::utils::testing::ScopedCommandStub;

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace planner = cachyos::installer::partition_planner;

namespace {

void install_noop_logger() {
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) { });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);
}

using Lines = std::vector<std::string>;

// sda1 ntfs at 1 MiB, the new sda2 at 10 GiB
constexpr auto kLsblkJson = R"({"blockdevices": [
  {"name": "/dev/sda1", "type": "part", "fstype": "ntfs", "uuid": null, "pkname": "/dev/sda", "start": 2048, "size": 10736369664, "parttypename": "Microsoft basic data"},
  {"name": "/dev/sda2", "type": "part", "fstype": null, "uuid": null, "pkname": "/dev/sda", "start": 20971520, "size": 1073741824, "parttypename": "EFI System"}
]})"sv;

}  // namespace

TEST_CASE("apply_partition_plan")
{
    install_noop_logger();
    ScopedCommandStub stub;
    stub.on_prefix("lsblk "s, {.output = std::string{kLsblkJson}});
    stub.on_prefix("udevadm "s, {});
    stub.on_prefix("/usr/bin/"s, {});
    stub.on_prefix("mkfs."s, {});

    using Kind = planner::PartitionOp::Kind;

    SECTION("shrink, create and mount the new partition as root")
    {
        const std::vector<planner::PartitionOp> ops{
            {.kind = Kind::Resize, .number = 1, .end_bytes = 10737418239, .size_bytes = 10736369664, .fstype = "ntfs"},
            {.kind = Kind::Create, .start_bytes = 10737418240, .end_bytes = 11811160063, .fstype = "ext4", .flags = "boot, esp"},
        };
        const std::vector<planner::StagedMount> mounts{
            {.created = true, .mountpoint = "/", .fstype = "ext4", .format_requested = true},
        };
        const auto res = planner::apply_partition_plan("/dev/sda"sv, ops, mounts);
        REQUIRE(res);
        REQUIRE_EQ(res->root.device, "/dev/sda2"s);
        REQUIRE_EQ(res->root.fstype, "ext4"s);

        const auto lines = stub.command_lines();
        REQUIRE(std::ranges::contains(lines, "/usr/bin/parted -s -a optimal /dev/sda unit B resizepart 1 10737418239B"s));
        REQUIRE(std::ranges::contains(lines, "mkfs.ext4 -q /dev/sda2"s));
        REQUIRE(std::ranges::contains(lines, "/usr/bin/parted -s -a optimal /dev/sda set 2 boot on"s));
        REQUIRE(std::ranges::contains(lines, "/usr/bin/parted -s -a optimal /dev/sda set 2 esp on"s));
    }
    SECTION("refuses a plan without root")
    {
        const std::vector<planner::StagedMount> mounts{
            {.device = "/dev/sda1", .mountpoint = "/home", .fstype = "ext4"},
        };
        REQUIRE_FALSE(planner::apply_partition_plan("/dev/sda"sv, {}, mounts));
    }
    SECTION("created row without a Create op")
    {
        const std::vector<planner::StagedMount> mounts{{.created = true, .mountpoint = "/", .fstype = "ext4"}};
        REQUIRE_FALSE(planner::apply_partition_plan("/dev/sda"sv, {}, mounts));
        REQUIRE(stub.command_lines().empty());
    }
}

TEST_CASE("list_partitions and list_free_space")
{
    install_noop_logger();
    ScopedCommandStub stub;
    stub.on_prefix("lsblk -f "s, {.output = std::string{kLsblkJson}});
    // 20 GiB disk
    stub.on_prefix("lsblk -d "s, {.output = R"({"blockdevices": [{"name": "/dev/sda", "type": "disk", "size": 21474836480}]})"s});

    const auto parts = planner::list_partitions("/dev/sda"sv);
    REQUIRE_EQ(parts.size(), 2U);
    REQUIRE_EQ(parts[1].number, 2U);
    REQUIRE_EQ(parts[1].start_bytes, 10737418240U);
    REQUIRE_EQ(parts[1].flags, "boot, esp"s);
    REQUIRE(parts[0].flags.empty());

    // the gap behind sda2, minus the GPT backup reserve
    const auto gaps = planner::list_free_space("/dev/sda"sv);
    REQUIRE_EQ(gaps.size(), 1U);
    REQUIRE_EQ(gaps[0].start_bytes, 11811160064U);
    REQUIRE_EQ(gaps[0].end_bytes, 21474836480U - (1024U * 1024U) - 1U);
}
