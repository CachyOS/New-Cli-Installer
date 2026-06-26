#include "gucc/plymouth.hpp"
#include "gucc/io_utils.hpp"

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace gucc::plymouth {

auto is_installed(std::string_view root_mountpoint) noexcept -> bool {
    std::error_code ec;
    return fs::exists(fs::path{root_mountpoint} / "usr" / "bin" / "plymouth", ec);
}

auto list_themes(std::string_view root_mountpoint) noexcept -> std::vector<std::string> {
    const auto themes_dir = fs::path{root_mountpoint} / "usr" / "share" / "plymouth" / "themes";
    std::error_code ec;
    if (!fs::is_directory(themes_dir, ec)) {
        return {};
    }

    std::vector<std::string> themes;
    for (const auto& entry : fs::directory_iterator{themes_dir, ec}) {
        if (ec) {
            break;
        }
        if (!entry.is_directory(ec)) {
            continue;
        }
        const auto name       = entry.path().filename().string();
        const auto descriptor = entry.path() / fmt::format(FMT_COMPILE("{}.plymouth"), name);
        if (!fs::exists(descriptor, ec)) {
            continue;
        }
        themes.push_back(name);
    }
    std::ranges::sort(themes);
    return themes;
}

auto set_default_theme(std::string_view theme, std::string_view root_mountpoint) noexcept -> Result<void> {
    const auto cmd = fmt::format(FMT_COMPILE("plymouth-set-default-theme {}"), theme);
    if (!utils::arch_chroot_checked(cmd, root_mountpoint)) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("plymouth: failed to set default theme to '{}' on '{}'", theme, root_mountpoint));
    }
    spdlog::info("plymouth: default theme set to '{}'", theme);
    return {};
}

}  // namespace gucc::plymouth
