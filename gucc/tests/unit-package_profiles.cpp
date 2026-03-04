#include "doctest_compatibility.h"

#include "gucc/file_utils.hpp"
#include "gucc/logger.hpp"
#include "gucc/package_profiles.hpp"

#include <filesystem>
#include <string_view>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

static constexpr auto VALID_PROFILE_TEST = R"(
[base-packages]
packages = ["a","b"]
[base-packages.desktop]
packages = ["c","d","f"]
[desktop.someprofile-1]
packages = ["ca","da","fa"]
[desktop.someprofile-2]
packages = ["cb","db","fb"]
)"sv;

static constexpr auto VALID_PROFILE_WITH_SERVICES_TEST = R"(
[base-packages]
packages = ["a","b"]
[base-packages.desktop]
packages = ["c","d","f"]
[services]
units = [
  { name = "NetworkManager", action = "enable", urgent = true },
  { name = "fstrim.timer", action = "enable" },
  { name = "pacman-init", action = "disable", urgent = true },
]
[services.desktop]
units = [
  { name = "bluetooth", action = "enable" },
  { name = "arch-update.timer", action = "enable", user = true },
]
[desktop.someprofile-1]
packages = ["ca","da","fa"]
[desktop.someprofile-2]
packages = ["cb","db","fb"]
)"sv;

static constexpr auto INVALID_PROFILE_TEST = R"(
[base-packages]
pacages = ["a,"b"]
[base-packages.desktop
package = c","d",f"]
[desktop.someprofile-1]
pacages = ["ca,"da","fa"
[desktop.someprofile-2]
packaes = ["cb","db",fb"]
)"sv;

TEST_CASE("package profiles test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("valid profile")
    {
        auto base_profs = gucc::profile::parse_base_profiles(VALID_PROFILE_TEST);
        REQUIRE(base_profs);
        REQUIRE_EQ(base_profs->base_packages, std::vector<std::string>{"a", "b"});
        REQUIRE_EQ(base_profs->base_desktop_packages, std::vector<std::string>{"c", "d", "f"});

        auto base_desktop_profs = gucc::profile::parse_desktop_profiles(VALID_PROFILE_TEST);
        REQUIRE(base_desktop_profs);
        REQUIRE_EQ(base_desktop_profs->size(), 2);
        REQUIRE_EQ((*base_desktop_profs)[0].profile_name, "someprofile-1");
        REQUIRE_EQ((*base_desktop_profs)[0].packages, std::vector<std::string>{"ca", "da", "fa"});
        REQUIRE_EQ((*base_desktop_profs)[1].profile_name, "someprofile-2");
        REQUIRE_EQ((*base_desktop_profs)[1].packages, std::vector<std::string>{"cb", "db", "fb"});

        auto net_profs = gucc::profile::parse_net_profiles(VALID_PROFILE_TEST);
        REQUIRE(net_profs);
        REQUIRE_EQ(net_profs->base_profiles.base_packages, std::vector<std::string>{"a", "b"});
        REQUIRE_EQ(net_profs->base_profiles.base_desktop_packages, std::vector<std::string>{"c", "d", "f"});
        REQUIRE_EQ(net_profs->desktop_profiles.size(), 2);
        REQUIRE_EQ(net_profs->desktop_profiles[0].profile_name, "someprofile-1");
        REQUIRE_EQ(net_profs->desktop_profiles[0].packages, std::vector<std::string>{"ca", "da", "fa"});
        REQUIRE_EQ(net_profs->desktop_profiles[1].profile_name, "someprofile-2");
        REQUIRE_EQ(net_profs->desktop_profiles[1].packages, std::vector<std::string>{"cb", "db", "fb"});
    }
    SECTION("valid profile with services")
    {
        auto base_profs = gucc::profile::parse_base_profiles(VALID_PROFILE_WITH_SERVICES_TEST);
        REQUIRE(base_profs);
        REQUIRE_EQ(base_profs->base_packages, std::vector<std::string>{"a", "b"});
        REQUIRE_EQ(base_profs->base_desktop_packages, std::vector<std::string>{"c", "d", "f"});

        // Check base services
        REQUIRE_EQ(base_profs->base_services.size(), 3);
        REQUIRE_EQ(base_profs->base_services[0].name, "NetworkManager");
        REQUIRE_EQ(base_profs->base_services[0].action, gucc::profile::ServiceAction::Enable);
        REQUIRE_EQ(base_profs->base_services[0].is_user_service, false);
        REQUIRE_EQ(base_profs->base_services[0].is_urgent, true);
        REQUIRE_EQ(base_profs->base_services[1].name, "fstrim.timer");
        REQUIRE_EQ(base_profs->base_services[1].action, gucc::profile::ServiceAction::Enable);
        REQUIRE_EQ(base_profs->base_services[1].is_urgent, false);
        REQUIRE_EQ(base_profs->base_services[2].name, "pacman-init");
        REQUIRE_EQ(base_profs->base_services[2].action, gucc::profile::ServiceAction::Disable);
        REQUIRE_EQ(base_profs->base_services[2].is_urgent, true);

        // Check desktop services
        REQUIRE_EQ(base_profs->base_desktop_services.size(), 2);
        REQUIRE_EQ(base_profs->base_desktop_services[0].name, "bluetooth");
        REQUIRE_EQ(base_profs->base_desktop_services[0].action, gucc::profile::ServiceAction::Enable);
        REQUIRE_EQ(base_profs->base_desktop_services[0].is_user_service, false);
        REQUIRE_EQ(base_profs->base_desktop_services[1].name, "arch-update.timer");
        REQUIRE_EQ(base_profs->base_desktop_services[1].action, gucc::profile::ServiceAction::Enable);
        REQUIRE_EQ(base_profs->base_desktop_services[1].is_user_service, true);

        // Also verify via parse_net_profiles
        auto net_profs = gucc::profile::parse_net_profiles(VALID_PROFILE_WITH_SERVICES_TEST);
        REQUIRE(net_profs);
        REQUIRE_EQ(net_profs->base_profiles.base_services.size(), 3);
        REQUIRE_EQ(net_profs->base_profiles.base_desktop_services.size(), 2);
    }
    SECTION("valid profile without services")
    {
        // Profiles without [services] sections should still parse fine with empty service vectors
        auto base_profs = gucc::profile::parse_base_profiles(VALID_PROFILE_TEST);
        REQUIRE(base_profs);
        REQUIRE(base_profs->base_services.empty());
        REQUIRE(base_profs->base_desktop_services.empty());
    }
    SECTION("invalid profile")
    {
        auto base_profs = gucc::profile::parse_base_profiles(INVALID_PROFILE_TEST);
        REQUIRE(!base_profs);

        auto base_desktop_profs = gucc::profile::parse_desktop_profiles(INVALID_PROFILE_TEST);
        REQUIRE(!base_desktop_profs);

        auto net_profs = gucc::profile::parse_net_profiles(INVALID_PROFILE_TEST);
        REQUIRE(!net_profs);
    }
}
