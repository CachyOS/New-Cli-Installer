#include "cachyos/config.hpp"
#include "cachyos/packages.hpp"

// import gucc
#include "gucc/autologin.hpp"
#include "gucc/hwclock.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/locale.hpp"
#include "gucc/timezone.hpp"
#include "gucc/user.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace {

using cachyos::installer::SystemSettings;

auto apply_hw_clock(SystemSettings::HwClock hw_clock, std::string_view mountpoint) noexcept -> std::expected<void, std::string> {
    spdlog::info("Setting hw_clock: {}", (hw_clock == SystemSettings::HwClock::UTC) ? "utc"sv : "localtime"sv);
    switch (hw_clock) {
    case SystemSettings::HwClock::UTC: {
        if (!gucc::hwclock::set_hwclock_utc(mountpoint)) {
            return std::unexpected("failed to set UTC hwclock");
        }
        break;
    }
    case SystemSettings::HwClock::Localtime: {
        if (!gucc::hwclock::set_hwclock_localtime(mountpoint)) {
            return std::unexpected("failed to set localtime hwclock");
        }
        break;
    }
    }
    return {};
}

}  // namespace

namespace cachyos::installer {

auto apply_system_settings(const SystemSettings& settings, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    // Hostname
    if (!settings.hostname.empty()) {
        spdlog::info("Setting hostname {}", settings.hostname);
        if (!gucc::user::set_hostname(settings.hostname, mountpoint)) {
            return std::unexpected("failed to set hostname");
        }
    }

    // Locale
    if (!settings.locale.empty()) {
        spdlog::info("Setting locale: {}", settings.locale);
        if (!gucc::locale::set_locale(settings.locale, mountpoint)) {
            return std::unexpected("failed to set locale");
        }
    }

    // Xkbmap
    if (!settings.xkbmap.empty()) {
        spdlog::info("Setting xkbmap: {}", settings.xkbmap);
        if (!gucc::locale::set_xkbmap(settings.xkbmap, mountpoint)) {
            return std::unexpected(fmt::format("failed to set xkbmap: {}", settings.xkbmap));
        }
    }

    // Keymap (live session only)
    if (!settings.keymap.empty()) {
        spdlog::info("Setting keymap: {}", settings.keymap);
        if (!gucc::utils::exec_checked(fmt::format(FMT_COMPILE("localectl set-keymap {}"), settings.keymap))) {
            spdlog::error("Failed to set live session keymap: {}", settings.keymap);
            // non-fatal: live session keymap failure shouldn't block install?
        }
    }

    // Timezone
    if (!settings.timezone.empty()) {
        spdlog::info("Setting timezone: {}", settings.timezone);
        if (!gucc::timezone::set_timezone(settings.timezone, mountpoint)) {
            return std::unexpected(fmt::format("failed to set timezone: {}", settings.timezone));
        }
    }

    // Hardware clock
    if (settings.hw_clock.has_value()) {
        auto result = apply_hw_clock(*settings.hw_clock, mountpoint);
        if (!result) {
            return result;
        }
    }
    return {};
}

auto create_user(const UserSettings& settings, std::string_view mountpoint,
    bool hostcache, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    spdlog::info("Creating user: {}", settings.username);

    // Install shell config packages if using zsh or fish
    const bool is_zsh  = settings.shell.ends_with("zsh");
    const bool is_fish = settings.shell.ends_with("fish");
    if (is_zsh || is_fish) {
        const auto shell_name = is_zsh ? "zsh"sv : "fish"sv;
        auto pkg              = fmt::format(FMT_COMPILE("cachyos-{}-config"), shell_name);
        auto pkg_result       = install_packages({std::move(pkg)}, mountpoint, hostcache, child);
        if (!pkg_result) {
            spdlog::error("Failed to install shell config: {}", pkg_result.error());
        }
    }

    // TODO(vnepogodin): should be customizable from some sort of config
    // instead of hardcoding??
    const gucc::user::UserInfo user_info{
        .username      = settings.username,
        .password      = settings.password,
        .shell         = settings.shell,
        .sudoers_group = "wheel"sv,
    };
    const std::vector default_user_groups{"wheel"s, "rfkill"s, "sys"s, "users"s, "lp"s, "video"s, "network"s, "storage"s, "audio"s};

    if (!gucc::user::create_new_user(user_info, default_user_groups, mountpoint)) {
        return std::unexpected("failed to create user");
    }

    return {};
}

auto set_root_password(std::string_view password, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    if (!gucc::user::set_root_password(password, mountpoint)) {
        return std::unexpected("failed to set root password");
    }
    return {};
}

auto enable_autologin(std::string_view dm, std::string_view username, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    if (!gucc::user::enable_autologin(dm, username, mountpoint)) {
        return std::unexpected("failed to enable autologin");
    }
    return {};
}

}  // namespace cachyos::installer
