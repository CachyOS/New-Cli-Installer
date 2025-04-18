#include "drivers.hpp"
#include "config.hpp"
#include "utils.hpp"
#include "widgets.hpp"

// import gucc
#include "gucc/chwd.hpp"

/* clang-format off */
#include <fstream>                                 // for ofstream
#include <variant>                                 // for get
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

    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& cachepath  = std::get<std::string>(config_data["cachepath"]);

#ifdef NDEVENV
    const auto& cmd_formatted = fmt::format(FMT_COMPILE("chwd --pmcachedir '{}' --pmroot {} -f -i {} 2>>/tmp/cachyos-install.log 2>&1"), cachepath, mountpoint, driver);
    tui::detail::follow_process_log_widget({"/bin/sh", "-c", cmd_formatted});
    std::ofstream{fmt::format(FMT_COMPILE("{}/.video_installed"), mountpoint)};
#else
    spdlog::debug("chwd --pmcachedir \"{}\" --pmroot {} -f -i {} 2>>/tmp/cachyos-install.log 2>&1", cachepath, mountpoint, driver);
#endif
}

void auto_install_drivers() noexcept {
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

#ifdef NDEVENV
    if (!gucc::chwd::install_available_profiles(mountpoint)) {
        spdlog::error("Failed to install drivers");
        return;
    }
    std::ofstream{fmt::format(FMT_COMPILE("{}/.video_installed"), mountpoint)};  // NOLINT
#else
    spdlog::debug("arch-chroot {} chwd -f -i &>>/tmp/cachyos-install.log", mountpoint);
#endif
}

void install_graphics_menu() noexcept {
    [[maybe_unused]] auto* config_instance  = Config::instance();
    [[maybe_unused]] auto& config_data      = config_instance->data();
    [[maybe_unused]] const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

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
