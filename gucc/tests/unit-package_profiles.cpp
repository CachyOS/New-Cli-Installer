#include "gucc/file_utils.hpp"
#include "gucc/package_profiles.hpp"

#include <cassert>

#include <filesystem>
#include <fstream>
#include <string_view>

#include <fmt/core.h>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/split.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

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

int main() {
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("default", callback_sink));

    // valid profile
    {
        auto base_profs = gucc::profile::parse_base_profiles(VALID_PROFILE_TEST);
        assert(base_profs);
        assert((base_profs->base_packages == std::vector<std::string>{"a", "b"}));
        assert((base_profs->base_desktop_packages == std::vector<std::string>{"c", "d", "f"}));

        auto base_desktop_profs = gucc::profile::parse_desktop_profiles(VALID_PROFILE_TEST);
        assert(base_desktop_profs);
        assert(base_desktop_profs->size() == 2);
        assert(((*base_desktop_profs)[0].profile_name == "someprofile-1"));
        assert(((*base_desktop_profs)[0].packages == std::vector<std::string>{"ca", "da", "fa"}));
        assert(((*base_desktop_profs)[1].profile_name == "someprofile-2"));
        assert(((*base_desktop_profs)[1].packages == std::vector<std::string>{"cb", "db", "fb"}));

        auto net_profs = gucc::profile::parse_net_profiles(VALID_PROFILE_TEST);
        assert(net_profs);
        assert((net_profs->base_profiles.base_packages == std::vector<std::string>{"a", "b"}));
        assert((net_profs->base_profiles.base_desktop_packages == std::vector<std::string>{"c", "d", "f"}));
        assert(net_profs->desktop_profiles.size() == 2);
        assert((net_profs->desktop_profiles[0].profile_name == "someprofile-1"));
        assert((net_profs->desktop_profiles[0].packages == std::vector<std::string>{"ca", "da", "fa"}));
        assert((net_profs->desktop_profiles[1].profile_name == "someprofile-2"));
        assert((net_profs->desktop_profiles[1].packages == std::vector<std::string>{"cb", "db", "fb"}));
    }
    // invalid profile
    {
        auto base_profs = gucc::profile::parse_base_profiles(INVALID_PROFILE_TEST);
        assert(!base_profs);

        auto base_desktop_profs = gucc::profile::parse_desktop_profiles(INVALID_PROFILE_TEST);
        assert(!base_desktop_profs);

        auto net_profs = gucc::profile::parse_net_profiles(INVALID_PROFILE_TEST);
        assert(!net_profs);
    }
}
