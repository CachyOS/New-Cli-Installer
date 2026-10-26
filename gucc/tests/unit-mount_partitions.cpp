#include "doctest_compatibility.h"
#include "test_temp_root.hpp"

#include "gucc/btrfs.hpp"
#include "gucc/logger.hpp"
#include "gucc/mount_partitions.hpp"
#include "gucc/testing/command_stub.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
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

}  // namespace

TEST_CASE("build_mount_argv")
{
    SECTION("known fstype and options emits -t and -o")
    {
        const auto argv = gucc::mount::build_mount_argv("/dev/sda1"sv, "/mnt/boot"sv, "defaults,umask=0077"sv, "vfat"sv);
        REQUIRE_EQ(argv, Lines{"/usr/bin/mount", "-t", "vfat", "-o", "defaults,umask=0077", "/dev/sda1", "/mnt/boot"});
    }
    SECTION("empty fstype keeps the historical auto-detecting command")
    {
        const auto argv = gucc::mount::build_mount_argv("/dev/sda1"sv, "/mnt/boot"sv, "defaults,umask=0077"sv);
        REQUIRE_EQ(argv, Lines{"/usr/bin/mount", "-o", "defaults,umask=0077", "/dev/sda1", "/mnt/boot"});
    }
    SECTION("known fstype without options still passes -t")
    {
        const auto argv = gucc::mount::build_mount_argv("/dev/sda1"sv, "/mnt/boot"sv, ""sv, "vfat"sv);
        REQUIRE_EQ(argv, Lines{"/usr/bin/mount", "-t", "vfat", "/dev/sda1", "/mnt/boot"});
    }
    SECTION("no fstype and no options is a bare mount")
    {
        const auto argv = gucc::mount::build_mount_argv("/dev/sda1"sv, "/mnt/boot"sv, ""sv);
        REQUIRE_EQ(argv, Lines{"/usr/bin/mount", "/dev/sda1", "/mnt/boot"});
    }
    SECTION("paths with spaces stay one argument")
    {
        const auto argv = gucc::mount::build_mount_argv("/dev/disk/by-label/my disk"sv, "/mnt/my boot"sv, ""sv);
        REQUIRE_EQ(argv, Lines{"/usr/bin/mount", "/dev/disk/by-label/my disk", "/mnt/my boot"});
    }
}

TEST_CASE("mount_partition runs the mount argv")
{
    install_noop_logger();
    ScopedCommandStub stub;
    stub.on_prefix("/usr/bin/mount "s, {});

    REQUIRE(gucc::mount::mount_partition("/dev/sda2"sv, "/mnt"sv, "noatime"sv, "ext4"sv));
    REQUIRE_EQ(stub.command_lines(), Lines{"/usr/bin/mount -t ext4 -o noatime /dev/sda2 /mnt"});
}

TEST_CASE("umount")
{
    install_noop_logger();
    ScopedCommandStub stub;
    stub.on_prefix("/usr/bin/umount "s, {});

    SECTION("direct")
    {
        REQUIRE(gucc::mount::umount("/mnt/boot"sv));
        REQUIRE_EQ(stub.command_lines(), Lines{"/usr/bin/umount -v /mnt/boot"});
    }
    SECTION("lazy")
    {
        REQUIRE(gucc::mount::umount("/mnt/boot"sv, true));
        REQUIRE_EQ(stub.command_lines(), Lines{"/usr/bin/umount -v -l /mnt/boot"});
    }
}

TEST_CASE("umount reports a failing command")
{
    install_noop_logger();
    ScopedCommandStub stub;
    stub.on_prefix("/usr/bin/umount "s, CannedReply{.exit_code = 32});

    const auto res = gucc::mount::umount("/mnt"sv);
    REQUIRE_FALSE(res);
    REQUIRE_EQ(res.error().code, gucc::ErrorCode::SubprocessFailed);
}

TEST_CASE("bind_mount")
{
    install_noop_logger();
    ScopedCommandStub stub;
    stub.on_prefix("/usr/bin/mount "s, {});

    REQUIRE(gucc::mount::bind_mount("/run/lvm"sv, "/mnt/hostlvm"sv));
    REQUIRE_EQ(stub.command_lines(), Lines{"/usr/bin/mount --bind /run/lvm /mnt/hostlvm"});
}

TEST_CASE("ScopedMount")
{
    install_noop_logger();
    ScopedCommandStub stub;
    stub.on_prefix("/usr/bin/mount "s, {});
    stub.on_prefix("/usr/bin/umount "s, {});

    SECTION("basic mount-umount")
    {
        {
            auto mnt = gucc::mount::ScopedMount::mount("/dev/sda1"sv, "/mnt/esp"sv, "ro"sv, "vfat"sv);
            REQUIRE(mnt);
            REQUIRE_EQ(mnt->path(), "/mnt/esp"sv);
            REQUIRE_EQ(stub.command_lines(), Lines{"/usr/bin/mount -t vfat -o ro /dev/sda1 /mnt/esp"});
        }
        REQUIRE_EQ(stub.command_lines(), Lines{"/usr/bin/mount -t vfat -o ro /dev/sda1 /mnt/esp", "/usr/bin/umount -v /mnt/esp"});
    }
    SECTION("bind unmounts its target")
    {
        {
            auto mnt = gucc::mount::ScopedMount::bind("/dev"sv, "/mnt/dev"sv);
            REQUIRE(mnt);
        }
        REQUIRE_EQ(stub.command_lines(), Lines{"/usr/bin/mount --bind /dev /mnt/dev", "/usr/bin/umount -v /mnt/dev"});
    }
    SECTION("moved from object no-op")
    {
        {
            auto mnt = gucc::mount::ScopedMount::bind("/dev"sv, "/mnt/dev"sv);
            REQUIRE(mnt);
            {
                auto moved = std::move(*mnt);
                REQUIRE_EQ(moved.path(), "/mnt/dev"sv);
            }
            REQUIRE_EQ(stub.count_prefix("/usr/bin/umount"sv), 1);
        }
        REQUIRE_EQ(stub.count_prefix("/usr/bin/umount"sv), 1);
    }
    SECTION("move semantics")
    {
        auto first  = gucc::mount::ScopedMount::bind("/dev"sv, "/mnt/dev"sv);
        auto second = gucc::mount::ScopedMount::bind("/proc"sv, "/mnt/proc"sv);
        REQUIRE(first);
        REQUIRE(second);
        *first = std::move(*second);
        REQUIRE_EQ(first->path(), "/mnt/proc"sv);
        REQUIRE_EQ(stub.unmatched(), Lines{});
        REQUIRE_EQ(stub.count_prefix("/usr/bin/umount -v /mnt/dev"sv), 1);
    }
    SECTION("failed mount")
    {
        ScopedCommandStub failing;
        failing.on_prefix("/usr/bin/mount "s, CannedReply{.exit_code = 32});
        REQUIRE_FALSE(gucc::mount::ScopedMount::mount("/dev/sda1"sv, "/mnt"sv, ""sv, ""sv));
        REQUIRE_EQ(failing.count_prefix("/usr/bin/umount"sv), 0);
    }
}

TEST_CASE("ScopedMount destr")
{
    install_noop_logger();
    ScopedCommandStub stub;
    stub.on_prefix("/usr/bin/mount "s, {});
    stub.on_prefix("/usr/bin/umount "s, CannedReply{.exit_code = 32});

    {
        auto mnt = gucc::mount::ScopedMount::bind("/dev"sv, "/mnt/dev"sv);
        REQUIRE(mnt);
    }
    REQUIRE_EQ(stub.count_prefix("/usr/bin/umount"sv), 1);
}

TEST_CASE("btrfs_mount_subvols")
{
    install_noop_logger();
    const gucc::tests::TempRoot root{"gucc-btrfs-mount"};
    const auto mnt = root.path().string();

    ScopedCommandStub stub;
    stub.on_prefix("/usr/bin/mount "s, {});

    const std::vector<gucc::fs::BtrfsSubvolume> subvols{
        gucc::fs::BtrfsSubvolume{.subvolume = "/@"s, .mountpoint = "/"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@home"s, .mountpoint = "/home"s},
    };
    REQUIRE(gucc::fs::btrfs_mount_subvols(subvols, "/dev/sda2"sv, mnt, "noatime"sv));
    REQUIRE_EQ(stub.calls().at(0).argv, Lines{"/usr/bin/mount", "-o", "subvol=/@,noatime", "/dev/sda2", mnt});
    REQUIRE_EQ(stub.calls().at(1).argv, Lines{"/usr/bin/mount", "-o", "subvol=/@home,noatime", "/dev/sda2", mnt + "/home"});
}
