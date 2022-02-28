#include "drivers.hpp"
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

#ifdef NDEVENV
#include "follow_process_log.hpp"
#endif

namespace tui {

static void install_ati() noexcept {
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    utils::exec(fmt::format(FMT_COMPILE("sed -i 's/MODULES=\"\"/MODULES=\"radeon\"/' {}/etc/mkinitcpio.conf"), mountpoint));
#endif
}

#ifdef NDEVENV
static void install_all_drivers() noexcept {
    static constexpr auto packages = "xf86-input-libinput xf86-video-fbdev xf86-video-vesa xf86-video-amdgpu xf86-video-ati xf86-video-intel xf86-video-nouveau";

    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& hostcache  = std::get<std::int32_t>(config_data["hostcache"]);

    if (hostcache) {
        detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("pacstrap {} {}"), mountpoint, packages)});
        return;
    }
    detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("pacstrap -c {} {}"), mountpoint, packages)});
}
#endif

static void setup_graphics_card() noexcept {
    std::string_view driver{};
    {
        static constexpr auto UseSpaceBar = "\nUse [Spacebar] to de/select options listed.\n";
        const auto& cmd                   = utils::exec("mhwd -l | awk '/ video-/{print $1}' | awk '$0=$0' | sort | uniq");
        const auto& radiobox_list         = utils::make_multiline(cmd);

        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        auto ok_callback = [&] {
            driver = radiobox_list[static_cast<size_t>(selected)];
            screen.ExitLoopClosure()();
        };
        detail::radiolist_widget(radiobox_list, ok_callback, &selected, &screen, {.text = UseSpaceBar}, {.text_size = nothing});
    }
    /* clang-format off */
    if (driver.empty()) { return; }
    /* clang-format on */

    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    auto& graphics_card   = std::get<std::string>(config_data["GRAPHIC_CARD"]);

#ifdef NDEVENV
    const auto& mountpoint    = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& cachepath     = std::get<std::string>(config_data["cachepath"]);
    const auto& cmd_formatted = fmt::format(FMT_COMPILE("mhwd --pmcachedir \"{}\" --pmroot {} -f -i pci {} 2>>/tmp/cachyos-install.log 2>&1"), cachepath, mountpoint, driver);
    tui::detail::follow_process_log_widget({"/bin/sh", "-c", cmd_formatted});
    std::ofstream{fmt::format(FMT_COMPILE("{}/.video_installed"), mountpoint)};
#endif

    graphics_card = utils::exec("lspci | grep -i \"vga\" | sed 's/.*://' | sed 's/(.*//' | sed 's/^[ \\t]*//'");

    // All non-NVIDIA cards / virtualization
    if (!(utils::exec(fmt::format(FMT_COMPILE("echo \"{}\" | grep -i 'ati'"), graphics_card)).empty())) {
        install_ati();
#ifdef NDEVENV
    } else if (driver == "video-nouveau") {
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/MODULES=\"\"/MODULES=\"nouveau\"/' {}/etc/mkinitcpio.conf"), mountpoint));
#endif
    }
}

static void install_graphics_menu() noexcept {
    [[maybe_unused]] auto* config_instance  = Config::instance();
    [[maybe_unused]] auto& config_data      = config_instance->data();
    [[maybe_unused]] const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    const std::vector<std::string> menu_entries = {
        "Auto-install free drivers",
        "Auto-install proprietary drivers",
        "Select Display Driver",
        "Install all free drivers",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
#ifdef NDEVENV
        case 0:
            utils::arch_chroot("mhwd -a pci free 0300");
            std::ofstream{fmt::format(FMT_COMPILE("{}/.video_installed"), mountpoint)};
            break;
        case 1:
            utils::arch_chroot("mhwd -a pci nonfree 0300");
            std::ofstream{fmt::format(FMT_COMPILE("{}/.video_installed"), mountpoint)};
            break;
#endif
        case 2:
            setup_graphics_card();
            break;
#ifdef NDEVENV
        case 3:
            install_all_drivers();
            std::ofstream{fmt::format(FMT_COMPILE("{}/.video_installed"), mountpoint)};
            break;
#endif
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
