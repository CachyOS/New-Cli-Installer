#include "cachyos/config.hpp"
#include "cachyos/packages.hpp"
#include "cachyos/result.hpp"

// import gucc
#include "gucc/autologin.hpp"
#include "gucc/hwclock.hpp"
#include "gucc/keyboard.hpp"
#include "gucc/locale.hpp"
#include "gucc/timezone.hpp"
#include "gucc/user.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for move
#include <vector>       // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace {

using cachyos::installer::SystemSettings;

auto apply_hw_clock(SystemSettings::HwClock hw_clock, std::string_view mountpoint) noexcept -> std::expected<void, std::string> {
    const bool is_utc = hw_clock == SystemSettings::HwClock::UTC;
    spdlog::info("Setting hw_clock: {}", is_utc ? "utc"sv : "localtime"sv);
    return is_utc
        ? cachyos::installer::lift(gucc::hwclock::set_hwclock_utc(mountpoint), "failed to set UTC hwclock")
        : cachyos::installer::lift(gucc::hwclock::set_hwclock_localtime(mountpoint), "failed to set localtime hwclock");
}

}  // namespace

namespace cachyos::installer {

auto apply_system_settings(const SystemSettings& settings, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    // Hostname
    if (!settings.hostname.empty()) {
        spdlog::info("Setting hostname {}", settings.hostname);
        if (auto res = gucc::user::set_hostname(settings.hostname, mountpoint); !res) {
            return lift(std::move(res));
        }
    }

    // Locale
    if (!settings.locale.empty()) {
        spdlog::info("Setting locale: {}", settings.locale);
        if (auto res = gucc::locale::set_locale(settings.locale, mountpoint); !res) {
            return lift(std::move(res), "failed to set locale");
        }
    }

    // Xkbmap
    if (!settings.xkbmap.empty()) {
        spdlog::info("Setting xkbmap: {}", settings.xkbmap);
        if (auto res = gucc::locale::set_xkbmap(settings.xkbmap, mountpoint); !res) {
            return lift(std::move(res), fmt::format(FMT_COMPILE("failed to set xkbmap: {}"), settings.xkbmap));
        }
    }

    // Keymap (live session only)
    if (!settings.keymap.empty()) {
        spdlog::info("Setting keymap: {}", settings.keymap);
        // non-fatal: only the live session is affected
        if (auto res = gucc::keyboard::set_host_keymap(settings.keymap); !res) {
            spdlog::error("Failed to set live session keymap {}: {}", settings.keymap, res.error().context);
        }
    }

    // Timezone
    if (!settings.timezone.empty()) {
        spdlog::info("Setting timezone: {}", settings.timezone);
        if (auto res = gucc::timezone::set_timezone(settings.timezone, mountpoint); !res) {
            return lift(std::move(res), fmt::format(FMT_COMPILE("failed to set timezone: {}"), settings.timezone));
        }
    }

    // Hardware clock
    if (settings.hw_clock.has_value()) {
        return apply_hw_clock(*settings.hw_clock, mountpoint);
    }
    return {};
}

auto create_user(const UserSettings& settings, std::string_view mountpoint,
    bool hostcache) noexcept
    -> std::expected<void, std::string> {
    spdlog::info("Creating user: {}", settings.username);

    // Install shell config packages if using zsh or fish
    const bool is_zsh  = settings.shell.ends_with("zsh");
    const bool is_fish = settings.shell.ends_with("fish");
    if (is_zsh || is_fish) {
        const auto shell_name = is_zsh ? "zsh"sv : "fish"sv;
        if (auto res = install_packages({fmt::format(FMT_COMPILE("cachyos-{}-config"), shell_name)}, mountpoint, hostcache); !res) {
            return res;
        }
    }

    const gucc::user::UserInfo user_info{
        .username      = settings.username,
        .password      = settings.password,
        .shell         = settings.shell,
        .sudoers_group = "wheel"sv,
    };

    static const std::vector kDefaultUserGroups{"wheel"s, "rfkill"s, "sys"s, "users"s, "lp"s, "video"s, "network"s, "storage"s, "audio"s};

    const auto& groups = settings.groups.empty() ? kDefaultUserGroups : settings.groups;
    return lift(gucc::user::create_new_user(user_info, groups, mountpoint));
}

auto set_root_password(std::string_view password, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    return lift(gucc::user::set_root_password(password, mountpoint));
}

auto enable_autologin(std::string_view dm, std::string_view username, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    return lift(gucc::user::enable_autologin(dm, username, mountpoint), "failed to enable autologin");
}

}  // namespace cachyos::installer
