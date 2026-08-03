#include "doctest_compatibility.h"

#include "gucc/mount_partitions.hpp"

#include <string_view>

using namespace std::string_view_literals;

TEST_CASE("build_mount_command")
{
    SECTION("known fstype and options emits -t and -o")
    {
        const auto cmd = gucc::mount::build_mount_command("/dev/sda1"sv, "/mnt/boot"sv, "defaults,umask=0077"sv, "vfat"sv);
        REQUIRE_EQ(cmd, "mount -t vfat -o defaults,umask=0077 /dev/sda1 /mnt/boot"sv);
    }
    SECTION("empty fstype keeps the historical auto-detecting command")
    {
        const auto cmd = gucc::mount::build_mount_command("/dev/sda1"sv, "/mnt/boot"sv, "defaults,umask=0077"sv);
        REQUIRE_EQ(cmd, "mount -o defaults,umask=0077 /dev/sda1 /mnt/boot"sv);
    }
    SECTION("known fstype without options still passes -t")
    {
        const auto cmd = gucc::mount::build_mount_command("/dev/sda1"sv, "/mnt/boot"sv, ""sv, "vfat"sv);
        REQUIRE_EQ(cmd, "mount -t vfat /dev/sda1 /mnt/boot"sv);
    }
    SECTION("no fstype and no options is a bare mount")
    {
        const auto cmd = gucc::mount::build_mount_command("/dev/sda1"sv, "/mnt/boot"sv, ""sv);
        REQUIRE_EQ(cmd, "mount /dev/sda1 /mnt/boot"sv);
    }
}
