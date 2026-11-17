#include "doctest_compatibility.h"

#include "detail/chroot.hpp"

#include <sys/mount.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

using gucc::utils::detail::ChrootHost;
using gucc::utils::detail::ChrootMount;
using gucc::utils::detail::plan_chroot_mounts;
using gucc::utils::detail::resolv_bind_target;

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

auto plan_targets(std::string_view root, const ChrootHost& host, const std::set<std::string>& mounted = {}) -> std::vector<std::string> {
    return plan_chroot_mounts(root, host, [&mounted](const std::string& path) { return mounted.contains(path); })
        | std::ranges::views::transform(&ChrootMount::target)
        | std::ranges::to<std::vector<std::string>>();
}

}  // namespace

TEST_CASE("plan_chroot_mounts")
{
    SECTION("bare root gets every arch-chroot mount in order")
    {
        const ChrootHost host{.efivars = true, .resolv_target = "/mnt/etc/resolv.conf"s};
        const auto plan = plan_chroot_mounts("/mnt/"sv, host, [](const std::string&) { return false; });

        const std::vector<std::string> expected{"/mnt/proc"s, "/mnt/sys"s, "/mnt/sys/firmware/efi/efivars"s, "/mnt/dev"s, "/mnt/dev/pts"s,
            "/mnt/dev/shm"s, "/mnt/run"s, "/mnt/tmp"s, "/mnt/etc/resolv.conf"s};
        REQUIRE_EQ(plan | std::ranges::views::transform(&ChrootMount::target) | std::ranges::to<std::vector<std::string>>(), expected);

        REQUIRE_EQ(std::string_view{plan[1].fstype}, "sysfs"sv);
        REQUIRE((plan[1].flags & MS_RDONLY) != 0);
        REQUIRE(plan[2].optional);
        REQUIRE_EQ(std::string_view{plan[4].data}, "mode=0620,gid=5"sv);
        REQUIRE(plan[6].make_private);
        REQUIRE_EQ(plan[6].flags, MS_BIND);
        REQUIRE((plan[7].flags & MS_STRICTATIME) != 0);
        REQUIRE_EQ(std::string_view{plan[8].source}, "/etc/resolv.conf"sv);
    }
    SECTION("no efivarfs on bios, no resolv.conf without a target")
    {
        const auto plan = plan_targets("/mnt"sv, ChrootHost{});
        REQUIRE(std::ranges::find(plan, "/mnt/sys/firmware/efi/efivars"s) == plan.end());
        REQUIRE_EQ(plan.size(), 7);
    }
    SECTION("calamares-prepared target is not collided")
    {
        const std::string root{"/tmp/calamares-root-x"};
        const std::set<std::string> mounted{root + "/proc", root + "/sys", root + "/sys/firmware/efi/efivars", root + "/dev", root + "/run"};
        const std::vector<std::string> expected{root + "/dev/pts", root + "/dev/shm", root + "/tmp"};
        REQUIRE_EQ(plan_targets(root, ChrootHost{.efivars = true, .resolv_target = std::nullopt}, mounted), expected);
    }
    SECTION("children of freshly mounted parent are always mounted")
    {
        const auto plan = plan_targets("/mnt"sv, ChrootHost{.efivars = true, .resolv_target = std::nullopt}, {"/mnt/sys/firmware/efi/efivars"s});
        REQUIRE(std::ranges::find(plan, "/mnt/sys/firmware/efi/efivars"s) != plan.end());
    }
}

TEST_CASE("resolv_bind_target")
{
    const auto root = fs::temp_directory_path() / "gucc-unit-chroot";
    fs::remove_all(root);
    fs::create_directories(root / "etc");
    fs::create_directories(root / "run/systemd/resolve");
    const auto resolv = root / "etc/resolv.conf";

    SECTION("missing")
    {
        REQUIRE_FALSE(resolv_bind_target(root.string()).has_value());
    }
    SECTION("regular file")
    {
        std::ofstream{resolv} << "nameserver 1.1.1.1\n";
        REQUIRE_EQ(resolv_bind_target(root.string()), resolv.string());
    }
    SECTION("absolute symlink resolves inside the root")
    {
        std::ofstream{root / "run/systemd/resolve/stub-resolv.conf"} << "";
        fs::create_symlink("/run/systemd/resolve/stub-resolv.conf", resolv);
        REQUIRE_EQ(resolv_bind_target(root.string()), (root / "run/systemd/resolve/stub-resolv.conf").string());
    }
    SECTION("relative symlink to nothing")
    {
        fs::create_symlink("../run/missing", resolv);
        REQUIRE_FALSE(resolv_bind_target(root.string()).has_value());
    }
    fs::remove_all(root);
}
