#include "drivers.hpp"
#include "installer_state.hpp"
#include "utils.hpp"
#include "widgets.hpp"

// import gucc
#include "gucc/chwd.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/string_utils.hpp"

#include <string>  // for string
#include <vector>  // for vector

#include <fmt/ranges.h>

using namespace std::string_literals;
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
    /// TODO(vnepogodin): parse toml DBs
    const auto& profile_names = gucc::chwd::get_available_profile_names();
    if (profile_names.empty()) {
        spdlog::error("failed to get profile names");
        return;
    }
    const auto picked = tui::detail::select_radio(profile_names);
    /* clang-format off */
    if (!picked || picked->empty()) { return; }
    /* clang-format on */
    const auto& driver = *picked;

    const auto mountpoint = utils::get_mountpoint();

    // setup hostcache path
    const bool hostcache = tui::state().inputs.ctx.hostcache;
    const auto cachepath = hostcache
        ? std::string{"/var/cache/pacman/pkg/"}
        : gucc::utils::target_path(mountpoint, "/var/cache/pacman/pkg/"sv);

    const std::vector<std::string> argv{"/usr/bin/chwd"s, "--pmcachedir"s, cachepath, "--pmroot"s, std::string{mountpoint}, "-f"s, "-i"s, driver};
#ifdef NDEVENV
    tui::detail::follow_process_log_widget(argv);
    mark_video_installed(mountpoint);
#else
    spdlog::debug("{}", fmt::join(argv, " "));
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
    spdlog::debug("arch-chroot {} chwd -f -i", mountpoint);
#endif
}

void install_graphics_menu() noexcept {
    tui::detail::menu_loop({}, {
                                   {"Auto-install drivers", [] { auto_install_drivers(); return true; }},
                                   {"Select Display Driver", [] { setup_graphics_card(); return true; }},
                                   {"Back", tui::detail::kLeaveMenu},
                               });
}

}  // namespace

namespace tui {

void install_drivers_menu() noexcept {
    detail::menu_loop({}, {
                              {"Install Display Driver", [] { install_graphics_menu(); return true; }},
                              {"Back", detail::kLeaveMenu},
                          });
}

}  // namespace tui
