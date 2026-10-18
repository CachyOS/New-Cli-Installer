#include "gucc/timezone.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>   // for sort, transform
#include <cctype>      // for isupper
#include <filesystem>  // for exists, directory_iterator
#include <ranges>      // for ranges::*
#include <string>      // for string
#include <vector>      // for vector

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

auto zone_files(const fs::path& dir, fs::path base) noexcept {
    return fs::recursive_directory_iterator(dir)
        | std::ranges::views::filter([](const fs::directory_entry& entry) { return entry.is_regular_file(); })
        | std::ranges::views::transform([base = std::move(base)](const fs::directory_entry& entry) { return fs::relative(entry.path(), base).string(); });
}

auto timezone_region_iter() noexcept {
    return fs::directory_iterator(ZONEINFO_PATH)
        | std::ranges::views::filter(is_valid_region)
        | std::ranges::views::transform(&fs::directory_entry::path);
}

}  // namespace

namespace gucc::timezone {

auto set_timezone(std::string_view timezone, std::string_view mountpoint) noexcept -> Result<void> {
    // Verify the timezone exists
    const auto& zoneinfo_path = fs::path{ZONEINFO_PATH} / timezone;
    if (!fs::exists(zoneinfo_path)) {
        return make_error(ErrorCode::InvalidArgument, FMT_COMPILE("Invalid timezone '{}'"), timezone);
    }

    // Remove existing localtime symlink if it exists
    std::error_code ec{};
    const auto& localtime_path = fmt::format(FMT_COMPILE("{}/etc/localtime"), mountpoint);
    if (fs::exists(localtime_path)) {
        fs::remove(localtime_path, ec);
        if (ec) {
            return make_error(ErrorCode::FileIo, FMT_COMPILE("Failed to remove existing localtime: {}"), ec.message());
        }
    }

    // Create the symlink
    fs::create_symlink(zoneinfo_path, localtime_path, ec);
    if (ec) {
        return make_error(ErrorCode::FileIo, FMT_COMPILE("Failed to create timezone symlink: {}"), ec.message());
    }

    spdlog::info("Timezone set to '{}' at {}", timezone, mountpoint);
    return {};
}

auto get_available_timezones() noexcept -> std::vector<std::string> {
    const auto& timezones = utils::exec("timedatectl list-timezones 2>/dev/null");
    if (!timezones.empty()) {
        return utils::make_multiline(timezones);
    }

    // for whatever reason timedatectl didn't work or doesn't exist
    auto result = timezone_region_iter()
        | std::ranges::views::transform([](const fs::path& region_path) { return zone_files(region_path, ZONEINFO_PATH); })
        | std::ranges::views::join
        | std::ranges::to<std::vector<std::string>>();
    std::ranges::sort(result);
    return result;
}

auto get_timezone_regions() noexcept -> std::vector<std::string> {
    auto regions = timezone_region_iter()
        | std::ranges::views::transform([](const fs::path& region_path) { return region_path.filename().string(); })
        | std::ranges::to<std::vector<std::string>>();
    std::ranges::sort(regions);
    return regions;
}

auto get_timezone_zones(std::string_view region) noexcept -> std::vector<std::string> {
    const auto region_path = fs::path{ZONEINFO_PATH} / region;
    if (!fs::exists(region_path) || !fs::is_directory(region_path)) {
        spdlog::warn("Invalid timezone region: {}", region);
        return {};
    }

    auto zones = zone_files(region_path, region_path) | std::ranges::to<std::vector<std::string>>();
    std::ranges::sort(zones);
    return zones;
}

}  // namespace gucc::timezone
