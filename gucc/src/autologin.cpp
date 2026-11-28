#include "gucc/autologin.hpp"
#include "gucc/conf.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/user.hpp"

#include <algorithm>         // for any_of, sort
#include <array>             // for array
#include <filesystem>        // for exists, directory_iterator
#include <initializer_list>  // for initializer_list
#include <optional>          // for optional
#include <ranges>            // for ranges::*
#include <string>            // for string
#include <system_error>      // for error_code
#include <utility>           // for pair
#include <vector>            // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace {

namespace fs = std::filesystem;

auto set_config_values(std::string_view path, std::string_view section, std::initializer_list<std::pair<std::string_view, std::string_view>> values) noexcept -> gucc::Result<void> {
    std::error_code err{};
    auto content = fs::exists(path, err) ? gucc::file_utils::read_whole_file(path) : std::string{};
    for (const auto& [key, value] : values) {
        content = gucc::conf::set_value(content, section, key, value);
    }
    return gucc::file_utils::write_file(path, content);
}

constexpr std::array kSessionDirs{"/usr/share/wayland-sessions"sv, "/usr/share/xsessions"sv};

constexpr auto session_for_desktop(std::string_view desktop) noexcept -> std::string_view {
    // desktop keyword -> session file stem
    if (desktop == "kde"sv) {
        return "plasma"sv;
    }
    if (desktop == "budgie"sv) {
        return "budgie-desktop"sv;
    }
    if (desktop == "lxde"sv) {
        return "LXDE"sv;
    }
    return desktop;
}

auto list_sessions(std::string_view root_mountpoint, std::string_view dir) noexcept -> std::vector<std::string> {
    std::error_code err{};
    auto sessions = fs::directory_iterator{gucc::utils::target_path(root_mountpoint, dir), err}
        | std::ranges::views::filter([](const fs::directory_entry& entry) { return entry.path().extension() == ".desktop"sv; })
        | std::ranges::views::transform([](const fs::directory_entry& entry) { return entry.path().stem().string(); })
        | std::ranges::to<std::vector<std::string>>();
    std::ranges::sort(sessions);
    return sessions;
}

auto resolve_session(std::string_view root_mountpoint, std::optional<std::string_view> desktop) noexcept -> gucc::Result<std::string> {
    // the desktop's session if installed
    if (!desktop) {
        for (const auto dir : kSessionDirs) {
            auto sessions = list_sessions(root_mountpoint, dir);
            if (sessions.size() == 1) {
                return std::move(sessions.front());
            }
            if (!sessions.empty()) {
                return gucc::make_error(gucc::ErrorCode::InvalidArgument, fmt::format(FMT_COMPILE("autologin: several sessions in {}: {}"), dir, fmt::join(sessions, ", ")));
            }
        }
        return gucc::make_error(gucc::ErrorCode::NotFound, "autologin: no session installed");
    }

    const auto wanted = session_for_desktop(*desktop);
    const auto file   = fmt::format(FMT_COMPILE("{}.desktop"), wanted);
    if (std::ranges::any_of(kSessionDirs, [&](std::string_view dir) {
            std::error_code err{};
            return fs::exists(fs::path{gucc::utils::target_path(root_mountpoint, dir)} / file, err);
        })) {
        return std::string{wanted};
    }

    // NOTE: listed only to say what is there instead
    std::vector<std::string> installed{};
    for (const auto dir : kSessionDirs) {
        installed.append_range(list_sessions(root_mountpoint, dir));
    }
    return gucc::make_error(gucc::ErrorCode::NotFound, fmt::format(FMT_COMPILE("autologin: no session '{}' among installed [{}]"), wanted, fmt::join(installed, ", ")));
}

auto set_session_autologin(std::string_view config_path, std::string_view username, std::string_view root_mountpoint, std::optional<std::string_view> desktop) noexcept -> gucc::Result<void> {
    return resolve_session(root_mountpoint, desktop).and_then([&](const std::string& session) {
        return set_config_values(gucc::utils::target_path(root_mountpoint, config_path), "Autologin"sv, {{"User"sv, username}, {"Session"sv, session}});
    });
}

}  // namespace

namespace gucc::user {

auto enable_autologin(std::string_view displaymanager, std::string_view username, std::string_view root_mountpoint, std::optional<std::string_view> desktop) noexcept -> Result<void> {
    if (displaymanager == "gdm"sv) {
        return set_config_values(utils::target_path(root_mountpoint, "/etc/gdm/custom.conf"sv), "daemon"sv,
            {{"AutomaticLoginEnable"sv, "True"sv}, {"AutomaticLogin"sv, username}});
    }
    if (displaymanager == "lightdm"sv) {
        if (auto res = set_config_values(utils::target_path(root_mountpoint, "/etc/lightdm/lightdm.conf"sv), "Seat:*"sv,
                {{"autologin-user"sv, username}, {"autologin-user-timeout"sv, "0"sv}});
            !res) {
            return res;
        }
        if (auto res = user::create_group("autologin"sv, root_mountpoint, true); !res) {
            return res;
        }
        return utils::arch_chroot_or_error({"/usr/bin/gpasswd"s, "-a"s, std::string{username}, "autologin"s}, root_mountpoint, "add user to autologin group"sv);
    }
    if (displaymanager == "plasmalogin"sv) {
        return set_session_autologin("/etc/plasmalogin.conf"sv, username, root_mountpoint, desktop);
    }
    if (displaymanager == "sddm"sv) {
        return set_session_autologin("/etc/sddm.conf"sv, username, root_mountpoint, desktop);
    }
    if (displaymanager == "lxdm"sv) {
        return set_config_values(utils::target_path(root_mountpoint, "/etc/lxdm/lxdm.conf"sv), "base"sv, {{"autologin"sv, username}});
    }
    return {};
}

}  // namespace gucc::user
