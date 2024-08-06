#include "doctest_compatibility.h"

#include "gucc/file_utils.hpp"
#include "gucc/logger.hpp"
#include "gucc/package_profiles.hpp"

#include <filesystem>
#include <fstream>
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
        REQUIRE((base_profs->base_packages == std::vector<std::string>{"a", "b"}));
        REQUIRE((base_profs->base_desktop_packages == std::vector<std::string>{"c", "d", "f"}));

        auto base_desktop_profs = gucc::profile::parse_desktop_profiles(VALID_PROFILE_TEST);
        REQUIRE(base_desktop_profs);
        REQUIRE(base_desktop_profs->size() == 2);
        REQUIRE(((*base_desktop_profs)[0].profile_name == "someprofile-1"));
        REQUIRE(((*base_desktop_profs)[0].packages == std::vector<std::string>{"ca", "da", "fa"}));
        REQUIRE(((*base_desktop_profs)[1].profile_name == "someprofile-2"));
        REQUIRE(((*base_desktop_profs)[1].packages == std::vector<std::string>{"cb", "db", "fb"}));

        auto net_profs = gucc::profile::parse_net_profiles(VALID_PROFILE_TEST);
        REQUIRE(net_profs);
        REQUIRE((net_profs->base_profiles.base_packages == std::vector<std::string>{"a", "b"}));
        REQUIRE((net_profs->base_profiles.base_desktop_packages == std::vector<std::string>{"c", "d", "f"}));
        REQUIRE(net_profs->desktop_profiles.size() == 2);
        REQUIRE((net_profs->desktop_profiles[0].profile_name == "someprofile-1"));
        REQUIRE((net_profs->desktop_profiles[0].packages == std::vector<std::string>{"ca", "da", "fa"}));
        REQUIRE((net_profs->desktop_profiles[1].profile_name == "someprofile-2"));
        REQUIRE((net_profs->desktop_profiles[1].packages == std::vector<std::string>{"cb", "db", "fb"}));
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
