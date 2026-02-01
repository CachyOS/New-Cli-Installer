#include "gucc/timezone.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <filesystem>

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace gucc::timezone {

auto set_timezone(std::string_view timezone, std::string_view mountpoint) noexcept -> bool {
    const auto& zoneinfo_path  = fmt::format(FMT_COMPILE("/usr/share/zoneinfo/{}"), timezone);
    const auto& localtime_path = fmt::format(FMT_COMPILE("{}/etc/localtime"), mountpoint);

    // Verify the timezone exists
    if (!fs::exists(zoneinfo_path)) {
        spdlog::error("Invalid timezone '{}': file does not exist at {}", timezone, zoneinfo_path);
        return false;
    }

    // Remove existing localtime symlink if it exists
    std::error_code ec{};
    if (fs::exists(localtime_path)) {
        fs::remove(localtime_path, ec);
        if (ec) {
            spdlog::error("Failed to remove existing localtime: {}", ec.message());
            return false;
        }
    }

    // Create the symlink
    const auto& relative_zoneinfo = fmt::format(FMT_COMPILE("/usr/share/zoneinfo/{}"), timezone);
    fs::create_symlink(relative_zoneinfo, localtime_path, ec);
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

    // Fallback: enumerate /usr/share/zoneinfo
    std::vector<std::string> result{};
    static constexpr auto zoneinfo_path = "/usr/share/zoneinfo"sv;

    if (!fs::exists(zoneinfo_path)) {
        spdlog::warn("Zoneinfo path does not exist: {}", zoneinfo_path);
        return result;
    }

    for (const auto& region_entry : fs::directory_iterator(zoneinfo_path)) {
        if (!region_entry.is_directory()) {
            continue;
        }
        const auto& region_name = region_entry.path().filename().string();
        // Skip non-region directories
        if (region_name == "posix" || region_name == "right" || region_name.starts_with('+')) {
            continue;
        }

        for (const auto& zone_entry : fs::recursive_directory_iterator(region_entry.path())) {
            if (zone_entry.is_regular_file()) {
                auto zone_path = fs::relative(zone_entry.path(), zoneinfo_path).string();
                result.push_back(std::move(zone_path));
            }
        }
    }

    std::ranges::sort(result);
    return result;
}

auto get_timezone_regions() noexcept -> std::vector<std::string> {
    std::vector<std::string> regions{};
    static constexpr auto zoneinfo_path = "/usr/share/zoneinfo"sv;

    if (!fs::exists(zoneinfo_path)) {
        spdlog::warn("Zoneinfo path does not exist: {}", zoneinfo_path);
        return regions;
    }

    for (const auto& entry : fs::directory_iterator(zoneinfo_path)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto& name = entry.path().filename().string();
        // Skip non-region directories and special entries
        if (name == "posix" || name == "right" || name.starts_with('+')) {
            continue;
        }
        // Only include directories that start with uppercase (valid regions)
        if (!name.empty() && std::isupper(static_cast<unsigned char>(name[0]))) {
            regions.push_back(name);
        }
    }

    std::ranges::sort(regions);
    return regions;
}

auto get_timezone_zones(std::string_view region) noexcept -> std::vector<std::string> {
    std::vector<std::string> zones{};
    const auto& region_path = fmt::format(FMT_COMPILE("/usr/share/zoneinfo/{}"), region);

    if (!fs::exists(region_path) || !fs::is_directory(region_path)) {
        spdlog::warn("Invalid timezone region: {}", region);
        return zones;
    }

    for (const auto& entry : fs::recursive_directory_iterator(region_path)) {
        if (entry.is_regular_file()) {
            auto zone_path = fs::relative(entry.path(), region_path).string();
            zones.push_back(std::move(zone_path));
        }
    }

    std::ranges::sort(zones);
    return zones;
}

}  // namespace gucc::timezone
