#include "doctest_compatibility.h"

#include "gucc/logger.hpp"
#include "gucc/timezone.hpp"

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <string_view>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

TEST_CASE("timezone test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    static constexpr std::string_view test_mountpoint{"/tmp/test-timezone-unittest"};

    SECTION("set valid timezone")
    {
        fs::create_directories(fmt::format("{}/etc", test_mountpoint));

        // Use a timezone that should exist on most systems
        REQUIRE(gucc::timezone::set_timezone("UTC"sv, test_mountpoint));

        const auto& localtime_path = fmt::format("{}/etc/localtime", test_mountpoint);
        REQUIRE(fs::exists(localtime_path));
        REQUIRE(fs::is_symlink(localtime_path));

        auto target = fs::read_symlink(localtime_path);
        REQUIRE_EQ(target.string(), "/usr/share/zoneinfo/UTC"sv);

        // Cleanup
        fs::remove_all(test_mountpoint);
    }

    SECTION("set timezone with region")
    {
        fs::create_directories(fmt::format("{}/etc", test_mountpoint));

        // Use America/New_York as a common timezone
        if (fs::exists("/usr/share/zoneinfo/America/New_York")) {
            REQUIRE(gucc::timezone::set_timezone("America/New_York"sv, test_mountpoint));

            const auto& localtime_path = fmt::format("{}/etc/localtime", test_mountpoint);
            REQUIRE(fs::exists(localtime_path));
            REQUIRE(fs::is_symlink(localtime_path));

            auto target = fs::read_symlink(localtime_path);
            REQUIRE_EQ(target.string(), "/usr/share/zoneinfo/America/New_York"sv);
        }

        // Cleanup
        fs::remove_all(test_mountpoint);
    }

    SECTION("set invalid timezone")
    {
        fs::create_directories(fmt::format("{}/etc", test_mountpoint));

        // This timezone should not exist
        REQUIRE_FALSE(gucc::timezone::set_timezone("Invalid/Nonexistent"sv, test_mountpoint));

        // Cleanup
        fs::remove_all(test_mountpoint);
    }

    SECTION("get timezone regions")
    {
        auto regions = gucc::timezone::get_timezone_regions();
        // Should have common regions if zoneinfo exists
        if (fs::exists("/usr/share/zoneinfo")) {
            REQUIRE(!regions.empty());
            // Check for common regions
            auto has_common_region = std::ranges::any_of(regions, [](const auto& r) {
                return r == "America" || r == "Europe" || r == "Asia" || r == "UTC";
            });
            CHECK(has_common_region);
        }
    }

    SECTION("get zones in region")
    {
        if (fs::exists("/usr/share/zoneinfo/America")) {
            auto zones = gucc::timezone::get_timezone_zones("America"sv);
            REQUIRE(!zones.empty());
            // Check for common zones
            auto has_new_york = std::ranges::any_of(zones, [](const auto& z) {
                return z == "New_York";
            });
            CHECK(has_new_york);
        }
    }

    SECTION("get zones for invalid region")
    {
        auto zones = gucc::timezone::get_timezone_zones("InvalidRegion"sv);
        REQUIRE(zones.empty());
    }
}
