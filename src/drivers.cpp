#include "drivers.hpp"
#include "global_storage.hpp"
#include "utils.hpp"
#include "widgets.hpp"

// import gucc
#include "gucc/chwd.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/string_utils.hpp"

/* clang-format off */
#include <ftxui/component/screen_interactive.hpp>  // for ScreenInteractive
#include <ftxui/dom/elements.hpp>                  // for nothing
/* clang-format on */

#include <fmt/compile.h>
#include <fmt/core.h>

using namespace ftxui;
using namespace std::string_view_literals;

#ifdef NDEVENV
#include "follow_process_log.hpp"
#endif

namespace {

#ifdef NDEVENV
void mark_video_installed(std::string_view mountpoint) noexcept {
    if (!gucc::file_utils::create_file_for_overwrite(gucc::utils::target_path(mountpoint, "/.video_installed"sv), {})) {
        spdlog::error("Failed to create {}/.video_installed", mountpoint);
    }
}
#endif

void setup_graphics_card() noexcept {
    std::string driver{};

    /// TODO(vnepogodin): parse toml DBs
    {
        static constexpr auto use_spacebar = "\nUse [Spacebar] to de/select options listed.\n"sv;
        const auto& profile_names          = gucc::chwd::get_available_profile_names();
        if (profile_names.empty()) {
            spdlog::error("failed to get profile names");
            return;
        }
        const auto& radiobox_list = profile_names;

        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        auto ok_callback = [&] {
            driver = radiobox_list[static_cast<size_t>(selected)];
            screen.ExitLoopClosure()();
        };
        tui::detail::radiolist_widget(radiobox_list, ok_callback, &selected, &screen, {.text = use_spacebar}, {.text_size = nothing});
    }
    /* clang-format off */
    if (driver.empty()) { return; }
    /* clang-format on */

    const auto mountpoint = utils::get_mountpoint();

    // setup hostcache path
    const bool hostcache = config::get<std::int32_t>("hostcache") != 0;
    const auto cachepath = hostcache
        ? std::string{"/var/cache/pacman/pkg/"}
        : gucc::utils::target_path(mountpoint, "/var/cache/pacman/pkg/"sv);

#ifdef NDEVENV
    const auto& cmd_formatted = fmt::format(FMT_COMPILE("chwd --pmcachedir '{}' --pmroot {} -f -i {} 2>>/tmp/cachyos-install.log 2>&1"), cachepath, mountpoint, driver);
    tui::detail::follow_process_log_widget({"/bin/sh", "-c", cmd_formatted});
    mark_video_installed(mountpoint);
#else
    spdlog::debug("chwd --pmcachedir \"{}\" --pmroot {} -f -i {} 2>>/tmp/cachyos-install.log 2>&1", cachepath, mountpoint, driver);
#endif
}

void auto_install_drivers() noexcept {
    const auto mountpoint = utils::get_mountpoint();

#ifdef NDEVENV
    if (!tui::detail::follow_process_log_task([&]() -> bool {
            return gucc::chwd::install_available_profiles(mountpoint).has_value();
        })) {
        spdlog::error("Failed to install drivers");
        return;
    }
    mark_video_installed(mountpoint);
#else
    spdlog::debug("arch-chroot {} chwd -f -i &>>/tmp/cachyos-install.log", mountpoint);
#endif
}

void install_graphics_menu() noexcept {
    const std::vector<std::string> menu_entries = {
        "Auto-install drivers",
        "Select Display Driver",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            auto_install_drivers();
            break;
        case 1:
            setup_graphics_card();
            break;
        default:
            screen.ExitLoopClosure()();
            break;
        }
    };
    tui::detail::menu_widget(menu_entries, ok_callback, &selected, &screen);
}

}  // namespace

namespace tui {

void install_drivers_menu() noexcept {
    const std::vector<std::string> menu_entries = {
        "Install Display Driver",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            install_graphics_menu();
            break;
        default:
            screen.ExitLoopClosure()();
            break;
        }
    };
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen);
}

}  // namespace tui
