#include "gucc/timezone.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>   // for sort, transform
#include <cctype>      // for isupper
#include <filesystem>  // for exists, directory_iterator
#include <ranges>      // for ranges::*

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

static constexpr auto ZONEINFO_PATH = "/usr/share/zoneinfo"sv;

namespace {

constexpr auto is_valid_tz_entry(std::string_view name) noexcept -> bool {
    return !name.empty()
        && name != "posix"sv && name != "right"sv
        && !name.starts_with('+')
        && std::isupper(name[0]);
}

constexpr auto is_valid_region(const fs::directory_entry& entry) noexcept -> bool {
    if (!entry.is_directory()) {
        return false;
    }
    const auto name = entry.path().filename().string();
    return is_valid_tz_entry(name);
}

auto timezone_region_iter() noexcept {
    return fs::directory_iterator(ZONEINFO_PATH)
        | std::ranges::views::filter(is_valid_region)
        | std::ranges::views::transform(&fs::directory_entry::path);
}

}  // namespace

namespace gucc::timezone {

auto set_timezone(std::string_view timezone, std::string_view mountpoint) noexcept -> bool {
    // Verify the timezone exists
    const auto& zoneinfo_path = fs::path{ZONEINFO_PATH} / timezone;
    if (!fs::exists(zoneinfo_path)) {
        spdlog::error("Invalid timezone '{}'", timezone);
        return false;
    }

    // Remove existing localtime symlink if it exists
    std::error_code ec{};
    const auto& localtime_path = fmt::format(FMT_COMPILE("{}/etc/localtime"), mountpoint);
    if (fs::exists(localtime_path)) {
        fs::remove(localtime_path, ec);
        if (ec) {
            spdlog::error("Failed to remove existing localtime: {}", ec.message());
            return false;
        }
    }

    // Create the symlink
    fs::create_symlink(zoneinfo_path, localtime_path, ec);
    if (ec) {
        spdlog::error("Failed to create timezone symlink: {}", ec.message());
        return false;
    }

    spdlog::info("Timezone set to '{}' at {}", timezone, mountpoint);
    return true;
}

auto get_available_timezones() noexcept -> std::vector<std::string> {
    const auto& timezones = utils::exec("timedatectl list-timezones 2>/dev/null");
    if (!timezones.empty()) {
        return utils::make_multiline(timezones);
    }

    // for whatever reason timedatectl didn't work or doesn't exist
    std::vector<std::string> result{};
    for (const auto& region_path : timezone_region_iter()) {
        for (const auto& zone_entry : fs::recursive_directory_iterator(region_path)) {
            if (!zone_entry.is_regular_file()) {
                continue;
            }
            auto zone_path = fs::relative(zone_entry.path(), ZONEINFO_PATH).string();
            result.push_back(std::move(zone_path));
        }
    }

    std::ranges::sort(result);
    return result;
}

auto get_timezone_regions() noexcept -> std::vector<std::string> {
    std::vector<std::string> regions{};
    for (const auto& region_path : timezone_region_iter()) {
        auto name = region_path.filename().string();
        regions.emplace_back(std::move(name));
    }

    std::ranges::sort(regions);
    return regions;
}

auto get_timezone_zones(std::string_view region) noexcept -> std::vector<std::string> {
    const auto region_path = fs::path{ZONEINFO_PATH} / region;
    if (!fs::exists(region_path) || !fs::is_directory(region_path)) {
        spdlog::warn("Invalid timezone region: {}", region);
        return {};
    }

    std::vector<std::string> zones{};
    for (const auto& entry : fs::recursive_directory_iterator(region_path)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto zone_path = fs::relative(entry.path(), region_path).string();
        zones.push_back(std::move(zone_path));
    }

    std::ranges::sort(zones);
    return zones;
}

}  // namespace gucc::timezone
