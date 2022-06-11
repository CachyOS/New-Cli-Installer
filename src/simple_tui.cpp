#include "simple_tui.hpp"
#include "config.hpp"
#include "disk.hpp"
#include "tui.hpp"
#include "utils.hpp"
#include "widgets.hpp"

/* clang-format off */
#include <cstdlib>                                 // for setenv
#include <sys/mount.h>                             // for mount
#include <fstream>                                 // for ofstream
#include <algorithm>                               // for copy
#include <thread>                                  // for thread
#include <filesystem>                              // for exists, is_directory
#include <string>                                  // for basic_string
#include <ftxui/component/component.hpp>           // for Renderer, Button
#include <ftxui/component/screen_interactive.hpp>  // for Component, ScreenI...
#include <ftxui/dom/elements.hpp>                  // for operator|, size
/* clang-format on */

#include <fmt/ranges.h>

using namespace ftxui;

namespace {
// Set static list of filesystems rather than on-the-fly. Partially as most require additional flags, and
// partially because some don't seem to be viable.
// Set static list of filesystems rather than on-the-fly.
void select_filesystem() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    // prep variables
    config_data["fs_opts"] = std::vector<std::string>{};

    const std::vector<std::string> menu_entries = {
        "btrfs",
        "ext3",
        "ext4",
        "f2fs",
        "jfs",
        "nilfs2",
        "ntfs",
        "reiserfs",
        "vfat",
        "xfs",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    bool success{};
    auto ok_callback = [&] {
        const auto& src      = menu_entries[static_cast<std::size_t>(selected)];
        const auto& lines    = utils::make_multiline(src, false, " ");
        const auto& file_sys = lines[0];
        utils::select_filesystem(file_sys.c_str());
        success = true;
        screen.ExitLoopClosure()();
    };
    static constexpr auto filesystem_body = "\nSelect your filesystem\n";
    const auto& content_size              = size(HEIGHT, LESS_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    tui::detail::menu_widget(menu_entries, ok_callback, &selected, &screen, filesystem_body, {size(HEIGHT, LESS_THAN, 18), content_size});

    if (!success) {
        utils::select_filesystem("btrfs");
    }
}
}  // namespace

namespace tui {

void menu_simple() noexcept {
    // Prepare
    utils::umount_partitions();

    auto* config_instance       = Config::instance();
    auto& config_data           = config_instance->data();
    const auto& mountpoint      = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& device_info     = std::get<std::string>(config_data["DEVICE"]);
    const auto& fs_name         = std::get<std::string>(config_data["FILESYSTEM_NAME"]);
    const auto& mount_opts_info = std::get<std::string>(config_data["MOUNT_OPTS"]);

    const auto& hostname        = std::get<std::string>(config_data["HOSTNAME"]);
    const auto& locale          = std::get<std::string>(config_data["LOCALE"]);
    const auto& xkbmap          = std::get<std::string>(config_data["XKBMAP"]);
    const auto& timezone        = std::get<std::string>(config_data["TIMEZONE"]);

    const auto& user_name       = std::get<std::string>(config_data["USER_NAME"]);
    const auto& user_pass       = std::get<std::string>(config_data["USER_PASS"]);
    const auto& user_shell      = std::get<std::string>(config_data["USER_SHELL"]);
    const auto& root_pass       = std::get<std::string>(config_data["ROOT_PASS"]);

    if (device_info.empty()) {
        tui::select_device();
    }
    tui::auto_partition(false);

    // Target FS
    if (fs_name.empty()) {
        select_filesystem();
    } else {
        utils::select_filesystem(fs_name);
    }
    tui::mount_current_partition(true);
    /* clang-format on */

    utils::generate_fstab("genfstab -U");

    if (hostname.empty()) {
        tui::set_hostname();
    } else {
        utils::set_hostname(hostname);
    }

    if (locale.empty()) {
        tui::set_locale();
    } else {
        utils::set_locale(locale);
    }

    if (xkbmap.empty()) {
        tui::set_xkbmap();
    } else {
        utils::set_xkbmap(xkbmap);
    }

    if (timezone.empty()) {
        tui::set_timezone();
    } else {
        utils::set_timezone(timezone);
    }

    utils::set_hw_clock("utc");

    if (root_pass.empty()) {
        tui::set_root_password();
    } else {
        utils::set_root_password(root_pass);
    }

    if (user_name.empty()) {
        tui::create_new_user();
    } else {
        utils::create_new_user(user_name, user_pass, user_shell);
    }

    fmt::print("┌{0:─^{5}}┐\n"
               "│{1: ^{5}}│\n"
               "│{2: ^{5}}│\n"
               "│{3: ^{5}}│\n"
               "│{4: ^{5}}│\n"
               "└{0:─^{5}}┘\n",
        "",
        fmt::format("Mountpoint: {}", mountpoint),
        fmt::format("Your device: {}", device_info),
        fmt::format("Filesystem: {}", fs_name),
        fmt::format("Filesystem opts: {}", mount_opts_info), 80);

    //    tui::mount_partitions();
    //
    //    // Install process
    //    if (!utils::check_mount()) {
    //        spdlog::error("Your partitions are not mounted");
    //    }
    //    tui::install_base();
    //    tui::install_desktop();
    //    if (!utils::check_base()) {
    //        spdlog::error("Base is not installed");
    //    }
    //    tui::install_bootloader();
    //    tui::config_base_menu();
}

}  // namespace tui
