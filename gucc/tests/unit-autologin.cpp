#include "doctest_compatibility.h"

#include "gucc/autologin.hpp"
#include "gucc/file_utils.hpp"

#include "test_temp_root.hpp"

#include <filesystem>
#include <string>
#include <string_view>

using namespace std::string_view_literals;

namespace fs = std::filesystem;

namespace {

void touch(const fs::path& root, std::string_view relative) {
    const auto file = root / fs::path{relative}.relative_path();
    fs::create_directories(file.parent_path());
    REQUIRE(gucc::file_utils::write_file(file.string(), ""));
}

auto read(const fs::path& root, std::string_view relative) -> std::string {
    return gucc::file_utils::read_whole_file((root / fs::path{relative}.relative_path()).string());
}

}  // namespace

TEST_CASE("enable autologin session")
{
    const gucc::tests::TempRoot temp{"gucc-autologin-test"};
    const auto& root = temp.path();
    fs::create_directories(root / "etc");

    SECTION("desktop picks its session among several")
    {
        touch(root, "/usr/share/wayland-sessions/hyprland.desktop");
        touch(root, "/usr/share/wayland-sessions/plasma.desktop");
        touch(root, "/usr/share/xsessions/plasmax11.desktop");

        REQUIRE(gucc::user::enable_autologin("plasmalogin"sv, "user"sv, root.string(), "kde"sv));
        const auto conf = read(root, "/etc/plasmalogin.conf");
        CHECK(conf.contains("User=user"sv));
        CHECK(conf.contains("Session=plasma\n"sv));
    }
    SECTION("desktop session not installed refuses")
    {
        touch(root, "/usr/share/wayland-sessions/hyprland.desktop");

        CHECK_FALSE(gucc::user::enable_autologin("sddm"sv, "user"sv, root.string(), "kde"sv));
    }
    SECTION("without desktop single wayland session over xorg")
    {
        touch(root, "/usr/share/wayland-sessions/plasma.desktop");
        touch(root, "/usr/share/xsessions/plasmax11.desktop");

        REQUIRE(gucc::user::enable_autologin("sddm"sv, "user"sv, root.string()));
        CHECK(read(root, "/etc/sddm.conf").contains("Session=plasma\n"sv));
    }
    SECTION("without desktop several sessions")
    {
        touch(root, "/usr/share/wayland-sessions/hyprland.desktop");
        touch(root, "/usr/share/wayland-sessions/plasma.desktop");

        CHECK_FALSE(gucc::user::enable_autologin("plasmalogin"sv, "user"sv, root.string()));
    }
    SECTION("no session refuses")
    {
        CHECK_FALSE(gucc::user::enable_autologin("plasmalogin"sv, "user"sv, root.string(), "kde"sv));
        CHECK_FALSE(gucc::user::enable_autologin("sddm"sv, "user"sv, root.string()));
    }
}
