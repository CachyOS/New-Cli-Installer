#include "drivers.hpp"
#include "chwd_profiles.hpp"
#include "config.hpp"
#include "utils.hpp"
#include "widgets.hpp"

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

namespace tui {

static void setup_graphics_card() noexcept {
    std::string driver{};

    /// TODO(vnepogodin): parse toml DBs
    {
        static constexpr auto use_spacebar = "\nUse [Spacebar] to de/select options listed.\n"sv;
        const auto& profile_names          = ::detail::chwd::get_available_profile_names("graphic_drivers"sv);
        if (!profile_names.has_value()) {
            spdlog::error("failed to get profile names");
            return;
        }
        const auto& radiobox_list = profile_names.value();

        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        auto ok_callback = [&] {
            driver = radiobox_list[static_cast<size_t>(selected)];
            screen.ExitLoopClosure()();
        };
        detail::radiolist_widget(radiobox_list, ok_callback, &selected, &screen, {.text = use_spacebar}, {.text_size = nothing});
    }
    /* clang-format off */
    if (driver.empty()) { return; }
    /* clang-format on */

    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& cachepath  = std::get<std::string>(config_data["cachepath"]);

#ifdef NDEVENV
    const auto& cmd_formatted = fmt::format(FMT_COMPILE("chwd --pmcachedir \"{}\" --pmroot {} -f -i pci {} 2>>/tmp/cachyos-install.log 2>&1"), cachepath, mountpoint, driver);
    tui::detail::follow_process_log_widget({"/bin/sh", "-c", cmd_formatted});
    std::ofstream{fmt::format(FMT_COMPILE("{}/.video_installed"), mountpoint)};
#else
    spdlog::debug("chwd --pmcachedir \"{}\" --pmroot {} -f -i pci {} 2>>/tmp/cachyos-install.log 2>&1", cachepath, mountpoint, driver);
#endif
}

static void install_graphics_menu() noexcept {
    [[maybe_unused]] auto* config_instance  = Config::instance();
    [[maybe_unused]] auto& config_data      = config_instance->data();
    [[maybe_unused]] const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    const std::vector<std::string> menu_entries = {
        "Auto-install free drivers",
        "Auto-install proprietary drivers",
        "Select Display Driver",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
#ifdef NDEVENV
        case 0:
            utils::arch_chroot("chwd -a pci free 0300"sv);
            std::ofstream{fmt::format(FMT_COMPILE("{}/.video_installed"), mountpoint)};
            break;
        case 1:
            utils::arch_chroot("chwd -a pci nonfree 0300"sv);
            std::ofstream{fmt::format(FMT_COMPILE("{}/.video_installed"), mountpoint)};
            break;
#endif
        case 2:
            setup_graphics_card();
            break;
        default:
            screen.ExitLoopClosure()();
            break;
        }
    };
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen);
}

void install_drivers_menu() noexcept {
    const std::vector<std::string> menu_entries = {
        "Install Display Driver",
        //"Install Network Drivers",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            install_graphics_menu();
            break;
        /*case 1:
            setup_network_drivers();
            break;*/
        default:
            screen.ExitLoopClosure()();
            break;
        }
    };
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen);
}

}  // namespace tui
