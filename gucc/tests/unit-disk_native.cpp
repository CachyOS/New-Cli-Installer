#include "doctest_compatibility.h"

#include "gucc/zfs.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using namespace std::string_literals;

TEST_CASE("find symlinks")
{
    const auto root = fs::temp_directory_path() / "gucc-find-symlinks";
    fs::remove_all(root);
    fs::create_directories(root / "disk/by-id");
    fs::create_directories(root / "disk/by-uuid");

    const auto device = root / "sdz1";
    std::ofstream{device} << "dev";
    std::ofstream{root / "sdz2"} << "other";

    fs::create_symlink("../../sdz1", root / "disk/by-id/ata-disk-part1");
    fs::create_symlink(device, root / "disk/by-uuid/abcd");
    fs::create_symlink("../../sdz2", root / "disk/by-id/ata-disk-part2");
    fs::create_symlink("missing", root / "disk/by-id/dangling");

    SECTION("only links resolving to the device")
    {
        const auto res = std::vector{
            (root / "disk/by-id/ata-disk-part1").string(),
            (root / "disk/by-uuid/abcd").string(),
        };
        CHECK_EQ(gucc::fs::detail::find_symlinks_to(device.string(), root.string()), res);
    }
    SECTION("missing root or device yields nothing")
    {
        CHECK(gucc::fs::detail::find_symlinks_to(device.string(), (root / "nope").string()).empty());
        CHECK(gucc::fs::detail::find_symlinks_to((root / "nope").string(), root.string()).empty());
    }

    fs::remove_all(root);
}
