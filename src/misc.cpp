#include "misc.hpp"
#include "config.hpp"
#include "utils.hpp"
#include "widgets.hpp"

/* clang-format off */
#include <filesystem>                              // for exists, is_directory
#include <ftxui/component/component.hpp>           // for Renderer, Button
#include <ftxui/component/component_options.hpp>   // for ButtonOption
#include <ftxui/component/screen_interactive.hpp>  // for Component, ScreenI...
#include <ftxui/dom/elements.hpp>                  // for operator|, size
/* clang-format on */

#include <fmt/compile.h>
#include <fmt/core.h>

using namespace ftxui;
namespace fs = std::filesystem;

namespace tui {

// Revised to deal with partion sizes now being displayed to the user
bool confirm_mount([[maybe_unused]] const std::string_view& part_user) {
#ifdef NDEVENV
    const auto& ret_status = utils::exec(fmt::format(FMT_COMPILE("mount | grep {}"), part_user), true);
    if (ret_status != "0") {
        detail::infobox_widget("\nMount Failed!\n");
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return false;
    }
#endif
    auto* config_instance   = Config::instance();
    auto& config_data       = config_instance->data();
    const auto& partition   = std::get<std::string>(config_data["PARTITION"]);
    auto& partitions        = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);
    auto& number_partitions = std::get<std::int32_t>(config_data["NUMBER_PARTITIONS"]);

    detail::infobox_widget("\nMount Successful!\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // TODO: reimplement natively
    const auto& str      = utils::make_multiline(partitions);
    const auto& cmd      = fmt::format(FMT_COMPILE("echo \"{0}\" | sed \"s~{1} [0-9]*[G-M]~~\" | sed \"s~{1} [0-9]*\\.[0-9]*[G-M]~~\" | sed \"s~{1}$\' -\'~~\""), str, partition);
    const auto& res_text = utils::exec(cmd);
    partitions           = utils::make_multiline(res_text);
    number_partitions -= 1;
    return true;
}

// Fsck hook
void set_fsck_hook() noexcept {
    auto* config_instance    = Config::instance();
    auto& config_data        = config_instance->data();
    const auto& do_sethook   = detail::yesno_widget("\nDo you want to use fsck hook?\n", size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    config_data["FSCK_HOOK"] = do_sethook;
}

// Choose pacman cache
void set_cache() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    static constexpr auto content = "\nDo you want to use the pacman cache of the running system instead\nof the installation target?\nThis can reduce the size of the required downloads in the installation.\n";
    if (!detail::yesno_widget(content, size(HEIGHT, GREATER_THAN, 3))) {
        config_data["hostcache"] = 0;
        config_data["cachepath"] = "/mnt/var/cache/pacman/pkg/";
        return;
    }

    config_data["hostcache"] = 1;
    config_data["cachepath"] = "/var/cache/pacman/pkg/";
}

static void edit_mkinitcpio(ScreenInteractive& screen) noexcept {
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
#else
    const std::string& mountpoint = "/";
#endif

    utils::exec(fmt::format(FMT_COMPILE("vim \"{}/etc/mkinitcpio.conf\""), mountpoint), true);
    screen.Resume();

    static constexpr auto content = "\nRun mkinitcpio?\n";
    const auto& do_run            = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 2) | size(WIDTH, LESS_THAN, 40));
    if (do_run) {
        utils::arch_chroot("mkinitcpio -P");
    }
    screen.Suspend();
}

static void edit_grub(ScreenInteractive& screen) noexcept {
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
#else
    const std::string& mountpoint = "/";
#endif

    utils::exec(fmt::format(FMT_COMPILE("vim \"{}/etc/default/grub\""), mountpoint), true);
    screen.Resume();

    static constexpr auto content = "\nUpdate GRUB?\n";
    const auto& do_update         = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 2) | size(WIDTH, LESS_THAN, 40));
    if (do_update) {
        utils::grub_mkconfig();
    }
    screen.Suspend();
}

void edit_configs() noexcept {
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& uefi_mount = std::get<std::string>(config_data["UEFI_MOUNT"]);
#ifdef NDEVENV
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
#else
    const std::string& mountpoint = "/";
#endif

    // Clear the file variables
    std::vector<std::string> menu_entries{};
    std::vector<std::function<void()>> functions{};
    menu_entries.reserve(24);
    functions.reserve(24);
    const auto& check_and_add = [&](
                                    const std::string& file_path, const std::string& dir = "",
                                    bool is_custom = false, std::function<void()>&& cust_func = []() {}) {
        const auto& append_function = [&functions](const std::string& filename) {
            /* clang-format off */
            functions.push_back([filename]{ utils::exec(fmt::format(FMT_COMPILE("vim {}"), filename), true); });
            /* clang-format on */
        };
        /* clang-format off */
        if (!fs::exists(file_path)) { return; }
        if (dir.empty()) { menu_entries.push_back(fmt::format(FMT_COMPILE("{}"), fs::path{file_path}.filename().c_str())); }
        else menu_entries.push_back(fmt::format(FMT_COMPILE("{} {}"), fs::path{file_path}.filename().c_str(), dir.data()));

        if (!is_custom) { append_function(file_path); }
        else functions.push_back([cust_func]{ cust_func(); });
        /* clang-format on */
    };

    {
        const auto& home_entry = fmt::format(FMT_COMPILE("{}/home"), mountpoint);
        if (fs::exists(home_entry))
            for (const auto& dir_entry : fs::directory_iterator{home_entry}) {
                const auto& extend_xinit_conf = fmt::format(FMT_COMPILE("{}/.extend.xinitrc"), dir_entry.path().c_str());
                const auto& extend_xres_conf  = fmt::format(FMT_COMPILE("{}/.extend.Xresources"), dir_entry.path().c_str());
                const auto& xinit_conf        = fmt::format(FMT_COMPILE("{}/.xinitrc"), dir_entry.path().c_str());
                const auto& xres_conf         = fmt::format(FMT_COMPILE("{}/.Xresources"), dir_entry.path().c_str());
                check_and_add(extend_xinit_conf, dir_entry.path().filename());
                check_and_add(xinit_conf, dir_entry.path().filename());
                check_and_add(extend_xres_conf, dir_entry.path().filename());
                check_and_add(xres_conf, dir_entry.path().filename());
            }
    }

    const std::array local_paths{
        fmt::format(FMT_COMPILE("{}/etc/crypttab"), mountpoint),
        fmt::format(FMT_COMPILE("{}/etc/fstab"), mountpoint),
        fmt::format(FMT_COMPILE("{}/boot/refind_linux.conf"), mountpoint),
        fmt::format(FMT_COMPILE("{}{}/EFI/refind/refind_linux.conf"), mountpoint, uefi_mount),
        fmt::format(FMT_COMPILE("{}{}/loader/loader.conf"), mountpoint, uefi_mount),
        fmt::format(FMT_COMPILE("{}/etc/hostname"), mountpoint),
        fmt::format(FMT_COMPILE("{}/etc/hosts"), mountpoint),
        fmt::format(FMT_COMPILE("{}/etc/systemd/journald.conf"), mountpoint),
        fmt::format(FMT_COMPILE("{}/etc/conf.d/keymaps"), mountpoint),
        fmt::format(FMT_COMPILE("{}/etc/locale.conf"), mountpoint),
        fmt::format(FMT_COMPILE("{}/etc/lightdm/lightdm.conf"), mountpoint),
        fmt::format(FMT_COMPILE("{}/etc/lxdm/lxdm.conf"), mountpoint),
        fmt::format(FMT_COMPILE("{}/etc/rc.conf"), mountpoint),
        fmt::format(FMT_COMPILE("{}/etc/pacman.conf"), mountpoint),
        fmt::format(FMT_COMPILE("{}/etc/makepkg.conf"), mountpoint),
        fmt::format(FMT_COMPILE("{}/etc/sddm.conf"), mountpoint),
        fmt::format(FMT_COMPILE("{}/etc/sudoers.conf"), mountpoint),
        fmt::format(FMT_COMPILE("{}/boot/syslinux/syslinux.cfg"), mountpoint),
        fmt::format(FMT_COMPILE("{}/etc/vconsole.conf"), mountpoint),
        fmt::format(FMT_COMPILE("{}/etc/default/grub"), mountpoint),
        fmt::format(FMT_COMPILE("{}/etc/mkinitcpio.conf"), mountpoint)};

    for (size_t i = 0; i < local_paths.size() - 2; ++i) {
        check_and_add(local_paths[i]);
    }

    auto screen = ScreenInteractive::Fullscreen();
    check_and_add(local_paths[19], "", true, [&screen] { edit_grub(screen); });
    check_and_add(local_paths[20], "", true, [&screen] { edit_mkinitcpio(screen); });

    std::int32_t selected{};
    auto ok_callback = [&] {
        screen.Suspend();
        functions[static_cast<std::size_t>(selected)]();
        screen.Resume();
    };
    static constexpr auto seeconf_body = "\nSelect any file listed below to be reviewed or amended.\n";
    const auto& content_size           = size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40) | vscroll_indicator | yframe | flex;
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, seeconf_body, {content_size, size(HEIGHT, GREATER_THAN, 1)});
}

void edit_pacman_conf() noexcept {
    utils::exec("vim /etc/pacman.conf", true);

#ifdef NDEVENV
    // NOTE: don't care now, Will change in future..
    detail::infobox_widget("\nUpdating database ...\n");
    utils::exec("pacman -Syy", true);
#endif
}

void logs_menu() noexcept {
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
#else
    const std::string& mountpoint = "/";
#endif

    const std::vector<std::string> menu_entries = {
        "Dmesg",
        "Pacman log",
        "Xorg log",
        "Journalctl",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        screen.Suspend();
        switch (selected) {
        case 0:
            utils::exec(fmt::format(FMT_COMPILE("arch-chroot {} dmesg | fzf --reverse --header=\"Exit by pressing esc\" --prompt=\"Type to filter log entries > \""), mountpoint), true);
            break;
        case 1:
            utils::exec(fmt::format(FMT_COMPILE("fzf --reverse --header=\"Exit by pressing esc\" --prompt=\"Type to filter log entries > \" < {}/var/log/pacman.log"), mountpoint), true);
            break;
        case 2:
            utils::exec(fmt::format(FMT_COMPILE("fzf --reverse --header=\"Exit by pressing esc\" --prompt=\"Type to filter log entries > \" < {}/var/log/Xorg.0.log"), mountpoint), true);
            break;
        case 3:
            utils::exec(fmt::format(FMT_COMPILE("arch-chroot {} journalctl | fzf --reverse --header=\"Exit by pressing esc\" --prompt=\"Type to filter log entries > \""), mountpoint), true);
            break;
        default:
            screen.Resume();
            screen.ExitLoopClosure()();
            return;
        }
        screen.Resume();
    };
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen);
}

}  // namespace tui
