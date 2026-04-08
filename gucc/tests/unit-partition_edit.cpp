#include "doctest_compatibility.h"

#include "gucc/logger.hpp"
#include "gucc/partitioning.hpp"
#include "gucc/testing/command_stub.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using gucc::utils::testing::CannedReply;
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

using Lines = std::vector<std::string>;

// sda1 at 1 MiB, sda2 at 10 GiB (START in 512-byte sectors)
constexpr auto kLsblkJson = R"({"blockdevices": [
  {"name": "/dev/sda1", "type": "part", "fstype": "vfat", "uuid": null, "pkname": "/dev/sda", "start": 2048, "size": 1073741824},
  {"name": "/dev/sda2", "type": "part", "fstype": null, "uuid": null, "pkname": "/dev/sda", "start": 20971520, "size": 1073741824}
]})"sv;

}  // namespace

TEST_CASE("parted table edits")
{
    install_noop_logger();
    ScopedCommandStub stub;
    stub.on_prefix("/usr/bin/parted "s, {});

    SECTION("mklabel")
    {
        REQUIRE(gucc::disk::create_partition_table("/dev/sda"sv, "gpt"sv));
        REQUIRE_EQ(stub.calls().front().argv, Lines{"/usr/bin/parted", "-s", "-a", "optimal", "/dev/sda", "mklabel", "gpt"});
    }
    SECTION("rm")
    {
        REQUIRE(gucc::disk::delete_partition("/dev/sda"sv, 3));
        REQUIRE_EQ(stub.command_lines(), Lines{"/usr/bin/parted -s -a optimal /dev/sda rm 3"});
    }
    SECTION("set flag off")
    {
        REQUIRE(gucc::disk::set_partition_flag("/dev/sda"sv, 1, "esp"sv, false));
        REQUIRE_EQ(stub.command_lines(), Lines{"/usr/bin/parted -s -a optimal /dev/sda set 1 esp off"});
    }
}

TEST_CASE("create partition")
{
    install_noop_logger();
    ScopedCommandStub stub;
    stub.on_prefix("/usr/bin/parted "s, {});
    stub.on_prefix("udevadm "s, {});
    stub.on_prefix("lsblk "s, {.output = std::string{kLsblkJson}});

    SECTION("resolves the partition starting inside the range")
    {
        const auto res = gucc::disk::create_partition("/dev/sda"sv, 10737418240, 11811160063);
        REQUIRE(res);
        REQUIRE_EQ(*res, "/dev/sda2"s);
        REQUIRE_EQ(stub.calls().front().argv,
            Lines{"/usr/bin/parted", "-s", "-a", "optimal", "/dev/sda", "unit", "B", "mkpart", "primary", "10737418240B", "11811160063B"});
    }
    SECTION("nothing new in the range")
    {
        const auto res = gucc::disk::create_partition("/dev/sda"sv, 20000000000, 30000000000);
        REQUIRE_FALSE(res);
        REQUIRE_EQ(res.error().code, gucc::ErrorCode::NotFound);
    }
    SECTION("empty range is refused before parted")
    {
        REQUIRE_FALSE(gucc::disk::create_partition("/dev/sda"sv, 10, 10));
        REQUIRE(stub.command_lines().empty());
    }
}

TEST_CASE("shrink partition")
{
    install_noop_logger();
    ScopedCommandStub stub;
    stub.on_prefix("/usr/bin/"s, {});

    SECTION("ext4 checks and resizes the filesystem before the table")
    {
        REQUIRE(gucc::disk::shrink_partition("/dev/nvme0n1"sv, 2, "ext4"sv, 4096 * 1024, 9999));
        REQUIRE_EQ(stub.command_lines(), Lines{
                                             "/usr/bin/e2fsck -f -y /dev/nvme0n1p2",
                                             "/usr/bin/resize2fs /dev/nvme0n1p2 4096K",
                                             "/usr/bin/parted -s -a optimal /dev/nvme0n1 unit B resizepart 2 9999B",
                                         });
    }
    SECTION("ntfs confirms on stdin")
    {
        REQUIRE(gucc::disk::shrink_partition("/dev/sda"sv, 3, "ntfs"sv, 5000, 9999));
        const auto calls = stub.calls();
        REQUIRE_EQ(calls.front().argv, Lines{"/usr/bin/ntfsresize", "--force", "--no-progress-bar", "--size", "5000", "/dev/sda3"});
        REQUIRE_EQ(calls.front().input, "y\n"s);
    }
    SECTION("xfs cannot shrink, the table is left alone")
    {
        const auto res = gucc::disk::shrink_partition("/dev/sda"sv, 3, "xfs"sv, 5000, 9999);
        REQUIRE_FALSE(res);
        REQUIRE_EQ(res.error().code, gucc::ErrorCode::Unsupported);
        REQUIRE(stub.command_lines().empty());
    }
}

TEST_CASE("filesystem used bytes")
{
    install_noop_logger();
    ScopedCommandStub stub;

    SECTION("ext4 from dumpe2fs")
    {
        stub.on_prefix("/usr/bin/dumpe2fs "s, {.output = "Reserved block count:     100\nBlock count:              1000\nFree blocks:              400\nBlock size:               4096\n"s});
        const auto res = gucc::disk::filesystem_used_bytes("/dev/sda2"sv, "ext4"sv);
        REQUIRE(res);
        REQUIRE_EQ(*res, 600U * 4096U);
    }
    SECTION("ntfs from ntfsresize")
    {
        stub.on_prefix("/usr/bin/ntfsresize "s, {.output = "You might resize at 5234515968 bytes or 5235 MB (freeing 1 MB).\n"s});
        const auto res = gucc::disk::filesystem_used_bytes("/dev/sda3"sv, "ntfs"sv);
        REQUIRE(res);
        REQUIRE_EQ(*res, 5234515968U);
    }
    SECTION("unparsable report")
    {
        stub.on_prefix("/usr/bin/dumpe2fs "s, {.output = "garbage"s});
        const auto res = gucc::disk::filesystem_used_bytes("/dev/sda2"sv, "ext4"sv);
        REQUIRE_FALSE(res);
        REQUIRE_EQ(res.error().code, gucc::ErrorCode::ParseError);
    }
}

TEST_CASE("set filesystem label")
{
    install_noop_logger();
    ScopedCommandStub stub;
    stub.on_prefix("/usr/bin/"s, {});

    SECTION("xfs takes the label first")
    {
        REQUIRE(gucc::disk::set_filesystem_label("/dev/sda2"sv, "xfs"sv, "data"sv));
        REQUIRE_EQ(stub.command_lines(), Lines{"/usr/bin/xfs_admin -L data /dev/sda2"});
    }
    SECTION("ntfs")
    {
        REQUIRE(gucc::disk::set_filesystem_label("/dev/sda3"sv, "ntfs"sv, "win"sv));
        REQUIRE_EQ(stub.command_lines(), Lines{"/usr/bin/ntfslabel /dev/sda3 win"});
    }
    SECTION("unknown filesystem")
    {
        REQUIRE_FALSE(gucc::disk::set_filesystem_label("/dev/sda3"sv, "swap"sv, "x"sv));
        REQUIRE(stub.command_lines().empty());
    }
}
