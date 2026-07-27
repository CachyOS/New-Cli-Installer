#include "doctest_compatibility.h"

#include "gucc/logger.hpp"
#include "gucc/package_list.hpp"
#include "gucc/package_profiles.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

static constexpr auto VENDORED = R"(
[base-packages]
packages = ["base", "base-devel"]
[base-packages.desktop]
packages = ["xorg"]

[desktop.kde]
packages = ["plasma-desktop", "konsole"]

[desktop.gnome]
packages = ["gnome-shell"]

[desktop.xorg]
packages = ["xorg-server"]

[services]
units = [
  { name = "NetworkManager", action = "enable" },
]

[[netinstall.group]]
name = "Gaming"
description = "vendored gaming"
selected = false
packages = ["steam"]

  [[netinstall.group.subgroup]]
  name = "Gaming Extras"
  packages = ["lutris"]

[[netinstall.group]]
name = "Common"
description = "vendored common"
critical = true
packages = ["base"]
)"sv;

}  // namespace

TEST_CASE("merge net profiles")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("scalar")
    {

        static constexpr auto LOWER_SCALAR = R"(
            [netinstall]
            enabled = false
        )"sv;
        static constexpr auto HIGHER_SCALAR = R"(
            [netinstall]
            enabled = true
        )"sv;
        auto merged = gucc::profile::merge_net_profiles(LOWER_SCALAR, HIGHER_SCALAR);
        REQUIRE(merged);
        REQUIRE(merged->find("enabled = true") != std::string::npos);
        REQUIRE(merged->find("enabled = false") == std::string::npos);
    }
    SECTION("array replace no concat")
    {
        static constexpr auto HIGHER = R"(
            [desktop.kde]
            packages = ["plasma-desktop", "sddm"]
        )"sv;

        auto merged = gucc::profile::merge_net_profiles(VENDORED, HIGHER);
        REQUIRE(merged);

        auto desktop_profs = gucc::profile::parse_desktop_profiles(*merged);
        REQUIRE(desktop_profs);

        auto kde = std::ranges::find(*desktop_profs, "kde"sv, &gucc::profile::DesktopProfile::profile_name);
        REQUIRE(kde != desktop_profs->end());
        REQUIRE_EQ(kde->packages, std::vector<std::string>{"plasma-desktop", "sddm"});

        auto gnome = std::ranges::find(*desktop_profs, "gnome"sv, &gucc::profile::DesktopProfile::profile_name);
        REQUIRE(gnome != desktop_profs->end());
        REQUIRE_EQ(gnome->packages, std::vector<std::string>{"gnome-shell"});
    }
    SECTION("merge add desktop")
    {
        static constexpr auto HIGHER = R"(
            [desktop.mycustom]
            packages = ["custom-pkg"]
        )"sv;

        auto merged = gucc::profile::merge_net_profiles(VENDORED, HIGHER);
        REQUIRE(merged);

        auto desktop_profs = gucc::profile::parse_desktop_profiles(*merged);
        REQUIRE(desktop_profs);
        REQUIRE_EQ(desktop_profs->size(), 4);

        auto custom = std::ranges::find(*desktop_profs, "mycustom"sv, &gucc::profile::DesktopProfile::profile_name);
        REQUIRE(custom != desktop_profs->end());
        REQUIRE_EQ(custom->packages, std::vector<std::string>{"custom-pkg"});

        REQUIRE(std::ranges::contains(*desktop_profs, "kde"sv, &gucc::profile::DesktopProfile::profile_name));
        REQUIRE(std::ranges::contains(*desktop_profs, "gnome"sv, &gucc::profile::DesktopProfile::profile_name));

        auto base_profs = gucc::profile::parse_base_profiles(*merged);
        REQUIRE(base_profs);
        REQUIRE_EQ(base_profs->base_packages, std::vector<std::string>{"base", "base-devel"});
    }
    SECTION("group&subgroup replace")
    {
        static constexpr auto HIGHER = R"(
            [[netinstall.group]]
            name = "Gaming"
            description = "user gaming override"
            selected = true
            packages = ["steam", "heroic"]

              [[netinstall.group.subgroup]]
              name = "Gaming Extras User"
              packages = ["bottles"]

            [[netinstall.group]]
            name = "Extra"
            description = "brand new group"
            packages = ["htop"]
        )"sv;

        auto merged = gucc::profile::merge_net_profiles(VENDORED, HIGHER);
        REQUIRE(merged);

        auto groups = gucc::profile::parse_netinstall_groups(*merged);
        REQUIRE(groups);
        REQUIRE_EQ(groups->size(), 3);

        const auto& gaming = (*groups)[0];
        REQUIRE_EQ(gaming.name, "Gaming");
        REQUIRE_EQ(gaming.description, "user gaming override");
        REQUIRE_EQ(gaming.selected, true);
        REQUIRE_EQ(gaming.packages, std::vector<std::string>{"steam", "heroic"});
        REQUIRE_EQ(gaming.subgroups.size(), 1);
        REQUIRE_EQ(gaming.subgroups[0].name, "Gaming Extras User");
        REQUIRE_EQ(gaming.subgroups[0].packages, std::vector<std::string>{"bottles"});

        const auto& common = (*groups)[1];
        REQUIRE_EQ(common.name, "Common");
        REQUIRE_EQ(common.description, "vendored common");
        REQUIRE_EQ(common.critical, true);

        const auto& extra = (*groups)[2];
        REQUIRE_EQ(extra.name, "Extra");
        REQUIRE_EQ(extra.packages, std::vector<std::string>{"htop"});
    }
    SECTION("empty hi")
    {
        auto merged = gucc::profile::merge_net_profiles(VENDORED, ""sv);
        REQUIRE(merged);
        REQUIRE_EQ(*merged, std::string{VENDORED});
    }
    SECTION("empty lo")
    {
        auto merged = gucc::profile::merge_net_profiles(""sv, VENDORED);
        REQUIRE(merged);

        auto base_profs = gucc::profile::parse_base_profiles(*merged);
        REQUIRE(base_profs);
        REQUIRE_EQ(base_profs->base_packages, std::vector<std::string>{"base", "base-devel"});
    }
    SECTION("invalid documents do not merge")
    {
        static constexpr auto INVALID = R"(
            [base-packages
            packages = "a" "b"
        )"sv;

        auto merged_bad_lower = gucc::profile::merge_net_profiles(INVALID, VENDORED);
        REQUIRE(!merged_bad_lower);

        auto merged_bad_higher = gucc::profile::merge_net_profiles(VENDORED, INVALID);
        REQUIRE(!merged_bad_higher);
    }
    SECTION("3 layer")
    {
        static constexpr auto FETCHED = R"(
            [desktop.kde]
            packages = ["plasma-desktop", "discover"]

            [[netinstall.group]]
            name = "Common"
            description = "fetched common override"
            critical = true
            packages = ["base", "curl"]
        )"sv;

        static constexpr auto USER = R"(
            [desktop.kde]
            packages = ["plasma-desktop", "kate"]

            [desktop.mycustom]
            packages = ["custom-pkg"]
        )"sv;

        auto merged = gucc::profile::load_layered_net_profiles({VENDORED, FETCHED, USER});
        REQUIRE(merged);

        auto desktop_profs = gucc::profile::parse_desktop_profiles(*merged);
        REQUIRE(desktop_profs);
        auto kde = std::ranges::find(*desktop_profs, "kde"sv, &gucc::profile::DesktopProfile::profile_name);
        REQUIRE(kde != desktop_profs->end());
        REQUIRE_EQ(kde->packages, std::vector<std::string>{"plasma-desktop", "kate"});

        REQUIRE(std::ranges::contains(*desktop_profs, "gnome"sv, &gucc::profile::DesktopProfile::profile_name));
        REQUIRE(std::ranges::contains(*desktop_profs, "mycustom"sv, &gucc::profile::DesktopProfile::profile_name));

        auto groups = gucc::profile::parse_netinstall_groups(*merged);
        REQUIRE(groups);
        auto common = std::ranges::find(*groups, "Common"sv, &gucc::profile::NetinstallGroup::name);
        REQUIRE(common != groups->end());
        REQUIRE_EQ(common->description, "fetched common override");
        REQUIRE_EQ(common->packages, std::vector<std::string>{"base", "curl"});

        auto gaming = std::ranges::find(*groups, "Gaming"sv, &gucc::profile::NetinstallGroup::name);
        REQUIRE(gaming != groups->end());
        REQUIRE_EQ(gaming->packages, std::vector<std::string>{"steam"});
    }
    SECTION("skipped layers single source unchanged")
    {
        auto merged = gucc::profile::load_layered_net_profiles({""sv, VENDORED, ""sv});
        REQUIRE(merged);

        auto base_profs = gucc::profile::parse_base_profiles(*merged);
        REQUIRE(base_profs);
        REQUIRE_EQ(base_profs->base_packages, std::vector<std::string>{"base", "base-devel"});

        auto direct_base_profs = gucc::profile::parse_base_profiles(VENDORED);
        REQUIRE(direct_base_profs);
        REQUIRE_EQ(base_profs->base_packages, direct_base_profs->base_packages);
        REQUIRE_EQ(base_profs->base_desktop_packages, direct_base_profs->base_desktop_packages);
    }
    SECTION("empty list")
    {
        auto merged = gucc::profile::load_layered_net_profiles({});
        REQUIRE(!merged);
    }
    SECTION("merged doc with overridden list")
    {
        static constexpr auto USER_OVERRIDE = R"(
            [desktop.kde]
            packages = ["plasma-desktop", "sddm", "kate"]
        )"sv;

        auto merged = gucc::profile::load_layered_net_profiles({VENDORED, USER_OVERRIDE});
        REQUIRE(merged);

        namespace fs = std::filesystem;
        const auto path = fs::temp_directory_path() / fs::path{fmt::format("gucc-net_profiles_merge-{}.toml", std::random_device{}())};
        {
            std::ofstream out{path};
            out << *merged;
        }

        const auto url = fmt::format("file://{}", path.string());
        const gucc::package::NetProfileInfo info{
            .net_profs_url          = url,
            .net_profs_fallback_url = url,
        };

        auto pkgs = gucc::package::get_pkglist_desktop("kde"sv, info);
        REQUIRE(pkgs);
        REQUIRE(std::ranges::contains(*pkgs, "sddm"));
        REQUIRE(std::ranges::contains(*pkgs, "kate"));
        REQUIRE(std::ranges::contains(*pkgs, "plasma-desktop"));
        REQUIRE(!std::ranges::contains(*pkgs, "konsole"));

        std::error_code ec;
        fs::remove(path, ec);
    }
}
