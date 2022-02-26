#include "tui.hpp"
#include "config.hpp"
#include "crypto.hpp"
#include "definitions.hpp"
#include "disk.hpp"
#include "drivers.hpp"
#include "misc.hpp"
#include "utils.hpp"
#include "widgets.hpp"

/* clang-format off */
#include <sys/mount.h>                             // for mount
#include <fstream>                                 // for ofstream
#include <filesystem>                              // for exists, is_directory
#include <string>                                  // for basic_string
#include <ftxui/component/component.hpp>           // for Renderer, Button
#include <ftxui/component/screen_interactive.hpp>  // for Component, ScreenI...
#include <ftxui/dom/elements.hpp>                  // for operator|, size
/* clang-format on */

using namespace ftxui;
namespace fs = std::filesystem;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"

#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/algorithm/copy.hpp>
#include <range/v3/algorithm/search.hpp>

#pragma clang diagnostic pop
#else
namespace ranges = std::ranges;
#endif

#ifdef NDEVENV
#include "follow_process_log.hpp"
#endif

namespace tui {

bool exit_done() noexcept {
#ifdef NDEVENV
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    static constexpr auto close_inst_body = "\nClose installer?\n";
    const auto& mountpoint                = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& target_mnt                = fmt::format(FMT_COMPILE("findmnt --list -o TARGET | grep {} 2>/dev/null"), mountpoint);
    if (!target_mnt.empty()) {
        utils::final_check();
        const auto& checklist = std::get<std::string>(config_data["CHECKLIST"]);
        const auto& do_close  = detail::yesno_widget(fmt::format(FMT_COMPILE("{}{}\n"), close_inst_body, checklist), size(HEIGHT, LESS_THAN, 20) | size(WIDTH, LESS_THAN, 40));
        /* clang-format off */
        if (!do_close) { return false; }
        /* clang-format on */

        spdlog::info("exit installer.");

        static constexpr auto LogInfo = "Would you like to save\nthe installation-log\nto the installed system?\nIt will be copied to\n";
        const auto& do_save_log       = detail::yesno_widget(fmt::format(FMT_COMPILE("\n{} {}/cachyos-install.log\n"), LogInfo, mountpoint), size(HEIGHT, LESS_THAN, 20) | size(WIDTH, LESS_THAN, 40));
        if (do_save_log) {
            fs::copy_file("/tmp/cachyos-install.log", fmt::format(FMT_COMPILE("{}/cachyos-install.log"), mountpoint), fs::copy_options::overwrite_existing);
        }
        utils::umount_partitions();
        utils::clear_screen();
        return true;
    }
    const auto& do_close = detail::yesno_widget(close_inst_body, size(HEIGHT, LESS_THAN, 10) | size(WIDTH, LESS_THAN, 40));
    /* clang-format off */
    if (!do_close) { return false; }
    /* clang-format on */

    utils::umount_partitions();
    utils::clear_screen();
#endif
    return true;
}

void btrfs_subvolumes() noexcept {
    const std::vector<std::string> menu_entries = {
        "automatic",
        "manual",
    };
    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    std::string btrfsvols_mode{};
    auto ok_callback = [&] {
        btrfsvols_mode = menu_entries[static_cast<std::size_t>(selected)];
        screen.ExitLoopClosure()();
    };
    static constexpr auto btrfsvols_body = "\nAutomatic mode\nis designed to allow integration\nwith snapper, non-recursive snapshots,\nseparating system and user data and\nrestoring snapshots without losing data.\n";
    /* clang-format off */
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, btrfsvols_body);

    if (btrfsvols_mode.empty()) { return; }
    /* clang-format on */

    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& mount_opts_info = std::get<std::string>(config_data["MOUNT_OPTS"]);
    const auto& root_part       = std::get<std::string>(config_data["ROOT_PART"]);
    utils::btrfs_create_subvols({.root = root_part, .mount_opts = mount_opts_info}, btrfsvols_mode);
}

// Function will not allow incorrect UUID type for installed system.
void generate_fstab() noexcept {
    const std::vector<std::string> menu_entries = {
        "genfstab -U -p",
        "genfstab -p",
        "genfstab -L -p",
        "genfstab -t PARTUUID -p",
    };

    auto* config_instance   = Config::instance();
    auto& config_data       = config_instance->data();
    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    const auto& mountpoint  = std::get<std::string>(config_data["MOUNTPOINT"]);

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        if (system_info == "BIOS" && selected == 3) {
            static constexpr auto FstabErr = "\nThe Part UUID option is only for UEFI/GPT installations.\n";
            detail::msgbox_widget(FstabErr);
            return;
        }
#ifdef NDEVENV
        const auto& src = menu_entries[static_cast<std::size_t>(selected)];
        utils::exec(fmt::format(FMT_COMPILE("{0} {1} > {1}/etc/fstab"), src, mountpoint));
        spdlog::info("Created fstab file:\n");
        utils::exec(fmt::format(FMT_COMPILE("cat {}/etc/fstab >> /tmp/cachyos-install.log"), mountpoint));
#endif
        const auto& swap_file = fmt::format(FMT_COMPILE("{}/swapfile"), mountpoint);
        if (fs::exists(swap_file) && fs::is_regular_file(swap_file)) {
            spdlog::info("appending swapfile to the fstab..");
#ifdef NDEVENV
            utils::exec(fmt::format(FMT_COMPILE("sed -i \"s/\\\\{0}//\" {0}/etc/fstab"), mountpoint));
#endif
        }
        screen.ExitLoopClosure()();
    };

    static constexpr auto fstab_body = "\nThe FSTAB file (File System TABle) sets what storage devices\nand partitions are to be mounted, and how they are to be used.\n\nUUID (Universally Unique IDentifier) is recommended.\n\nIf no labels were set for the partitions earlier,\ndevice names will be used for the label option.\n";
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, fstab_body);

#ifdef NDEVENV
    // Edit fstab in case of btrfs subvolumes
    utils::exec(fmt::format(FMT_COMPILE("sed -i \"s/subvolid=.*,subvol=\\/.*,//g\" {}/etc/fstab"), mountpoint));
#endif
}

// Set system hostname
void set_hostname() noexcept {
    std::string hostname{"cachyos"};
    static constexpr auto hostname_body = "\nThe hostname is used to identify the system on a network.\n \nIt is restricted to alphanumeric characters, can contain a hyphen\n(-) - but not at the start or end - and must be no longer than 63 characters.\n";
    if (!detail::inputbox_widget(hostname, hostname_body, size(HEIGHT, GREATER_THAN, 4))) {
        return;
    }
    // If at least one package, install.
    /* clang-format off */
    if (hostname.empty()) { return; }
    /* clang-format on */

#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    utils::exec(fmt::format(FMT_COMPILE("echo \"{}\" > {}/etc/hostname"), hostname, mountpoint));
    const auto& cmd = fmt::format(FMT_COMPILE("echo -e \"#<ip-address>\\t<hostname.domain.org>\\t<hostname>\\n127.0.0.1\\tlocalhost.localdomain\\tlocalhost\\t{0}\\n::1\\tlocalhost.localdomain\\tlocalhost\\t{0}\">{1}/etc/hosts"), hostname, mountpoint);
    utils::exec(cmd);
#endif
}

// Set system language
void set_locale() noexcept {
    const auto& locales = utils::make_multiline(utils::exec("cat /etc/locale.gen | grep -v \"#  \" | sed 's/#//g' | awk '/UTF-8/ {print $1}'"));

    // System language
    std::string locale{};
    {
        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{95};
        auto ok_callback = [&] {
            locale = locales[static_cast<std::size_t>(selected)];
            screen.ExitLoopClosure()();
        };

        static constexpr auto langBody = "\nChoose the system language.\n\nThe format is language_COUNTRY (e.g. en_US is english, United States;\nen_GB is english, Great Britain).\n";
        const auto& content_size       = size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40) | vscroll_indicator | yframe | flex;
        detail::menu_widget(locales, ok_callback, &selected, &screen, langBody, {content_size, size(HEIGHT, GREATER_THAN, 1)});
    }
    /* clang-format off */
    if (!locale.empty()) { return; }
    /* clang-format on */

#ifdef NDEVENV
    auto* config_instance          = Config::instance();
    auto& config_data              = config_instance->data();
    const auto& mountpoint         = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& locale_config_path = fmt::format(FMT_COMPILE("{}/etc/locale.conf"), mountpoint);
    const auto& locale_gen_path    = fmt::format(FMT_COMPILE("{}/etc/locale.gen"), mountpoint);

    utils::exec(fmt::format(FMT_COMPILE("echo \"LANG=\\\"{}\\\"\" > {}"), locale, locale_config_path));
    utils::exec(fmt::format(FMT_COMPILE("echo \"LC_MESSAGES=\\\"{}\\\"\" >> {}"), locale, locale_config_path));
    utils::exec(fmt::format(FMT_COMPILE("sed -i \"s/#{0}/{0}/\" {1}"), locale, locale_gen_path));

    // Generate locales
    utils::arch_chroot("locale-gen", false);
#endif
}

// Set keymap for X11
void set_xkbmap() noexcept {
    static constexpr auto keymaps_xkb = "af al am at az ba bd be bg br bt bw by ca cd ch cm cn cz de dk ee es et eu fi fo fr gb ge gh gn gr hr hu ie il in iq ir is it jp ke kg kh kr kz la lk lt lv ma md me mk ml mm mn mt mv ng nl no np pc ph pk pl pt ro rs ru se si sk sn sy tg th tj tm tr tw tz ua us uz vn za";
    const auto& xkbmap_list           = utils::make_multiline(keymaps_xkb, false, " ");

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{86};
    bool success{};
    std::string_view xkbmap_choice{};
    auto ok_callback = [&] {
        const auto& keymap = xkbmap_list[static_cast<std::size_t>(selected)];
        xkbmap_choice      = utils::exec(fmt::format(FMT_COMPILE("echo \"{}\" | sed 's/_.*//'"), keymap));
        success            = true;
        screen.ExitLoopClosure()();
    };

    static constexpr auto xkbmap_body = "\nSelect Desktop Environment Keymap.\n";
    const auto& content_size          = size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40) | vscroll_indicator | yframe | flex;
    detail::menu_widget(xkbmap_list, ok_callback, &selected, &screen, xkbmap_body, {content_size, size(HEIGHT, GREATER_THAN, 1)});

    /* clang-format off */
    if (!success) { return; }
    /* clang-format on */

#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    utils::exec(fmt::format(FMT_COMPILE("echo -e \"Section \"\\\"InputClass\"\\\"\\nIdentifier \"\\\"system-keyboard\"\\\"\\nMatchIsKeyboard \"\\\"on\"\\\"\\nOption \"\\\"XkbLayout\"\\\" \"\\\"{0}\"\\\"\\nEndSection\" > {1}/etc/X11/xorg.conf.d/00-keyboard.conf"), xkbmap_choice, mountpoint));
#endif
}

void select_keymap() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    auto& keymap          = std::get<std::string>(config_data["KEYMAP"]);

    // does user want to change the default settings?
    static constexpr auto default_keymap = "Currently configured keymap setting is:";
    const auto& content                  = fmt::format(FMT_COMPILE("\n {}\n \n[ {} ]\n"), default_keymap, keymap);
    const auto& keep_default             = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    /* clang-format off */
    if (!keep_default) { return; }
    /* clang-format on */

    const auto& keymaps = utils::make_multiline(utils::exec("ls -R /usr/share/kbd/keymaps | grep \"map.gz\" | sed 's/\\.map\\.gz//g' | sort"));

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{226};
    auto ok_callback = [&] {
        keymap = keymaps[static_cast<std::size_t>(selected)];
        screen.ExitLoopClosure()();
    };
    static constexpr auto vc_keymap_body = "\nA virtual console is a shell prompt in a non-graphical environment.\nIts keymap is independent of a desktop environment / terminal.\n";
    const auto& content_size             = size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40) | vscroll_indicator | yframe | flex;
    detail::menu_widget(keymaps, ok_callback, &selected, &screen, vc_keymap_body, {content_size, size(HEIGHT, GREATER_THAN, 1)});
}

// Set Zone and Sub-Zone
bool set_timezone() noexcept {
    std::string zone{};
    {
        auto screen           = ScreenInteractive::Fullscreen();
        const auto& cmd       = utils::exec("cat /usr/share/zoneinfo/zone.tab | awk '{print $3}' | grep \"/\" | sed \"s/\\/.*//g\" | sort -ud");
        const auto& zone_list = utils::make_multiline(cmd);

        std::int32_t selected{};
        auto ok_callback = [&] {
            zone = zone_list[static_cast<std::size_t>(selected)];
            screen.ExitLoopClosure()();
        };

        static constexpr auto timezone_body = "The time zone is used to correctly set your system clock.";
        const auto& content_size            = size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
        detail::menu_widget(zone_list, ok_callback, &selected, &screen, timezone_body, {content_size, size(HEIGHT, GREATER_THAN, 1)});
    }
    /* clang-format off */
    if (zone.empty()) { return false; }
    /* clang-format on */

    std::string subzone{};
    {
        auto screen           = ScreenInteractive::Fullscreen();
        const auto& cmd       = utils::exec(fmt::format(FMT_COMPILE("cat /usr/share/zoneinfo/zone.tab | {1} | grep \"{0}/\" | sed \"s/{0}\\///g\" | sort -ud"), zone, "awk '{print $3}'"));
        const auto& city_list = utils::make_multiline(cmd);

        std::int32_t selected{};
        auto ok_callback = [&] {
            subzone = city_list[static_cast<std::size_t>(selected)];
            screen.ExitLoopClosure()();
        };

        static constexpr auto sub_timezone_body = "Select the city nearest to you.";
        const auto& content_size                = size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40) | vscroll_indicator | yframe | flex;
        detail::menu_widget(city_list, ok_callback, &selected, &screen, sub_timezone_body, {content_size, size(HEIGHT, GREATER_THAN, 1)});
    }

    /* clang-format off */
    if (subzone.empty()) { return false; }
    /* clang-format on */

    const auto& content         = fmt::format(FMT_COMPILE("\nSet Time Zone: {}/{}?\n"), zone, subzone);
    const auto& do_set_timezone = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    /* clang-format off */
    if (!do_set_timezone) { return false; }
    /* clang-format on */

#ifdef NDEVENV
    utils::arch_chroot(fmt::format(FMT_COMPILE("ln -sf /usr/share/zoneinfo/{}/{} /etc/localtime"), zone, subzone), false);
#endif
    spdlog::info("Timezone is set to {}/{}", zone, subzone);
    return true;
}

// Set system clock type
void set_hw_clock() noexcept {
    auto screen = ScreenInteractive::Fullscreen();
    const std::vector<std::string> menu_entries{"utc", "localtime"};
    std::int32_t selected{};
    auto ok_callback = [&] {
#ifdef NDEVENV
        const auto& clock_type = menu_entries[static_cast<std::size_t>(selected)];
        utils::arch_chroot(fmt::format(FMT_COMPILE("hwclock --systohc --{}"), clock_type), false);
#endif
        screen.ExitLoopClosure()();
    };

    static constexpr auto hw_clock_body = "UTC is the universal time standard,\nand is recommended unless dual-booting with Windows.";
    const auto& content_size            = size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, hw_clock_body, {content_size, size(HEIGHT, GREATER_THAN, 1)});
}

// Set password for root user
void set_root_password() noexcept {
    std::string pass{};
    static constexpr auto root_pass_body = "Enter Root password";
    if (!detail::inputbox_widget(pass, root_pass_body, size(HEIGHT, GREATER_THAN, 1), true)) {
        return;
    }
    std::string confirm{};
    static constexpr auto root_confirm_body = "Re-enter Root password";
    if (!detail::inputbox_widget(confirm, root_confirm_body, size(HEIGHT, GREATER_THAN, 1), true)) {
        return;
    }

    if (pass != confirm) {
        static constexpr auto PassErrBody = "\nThe passwords entered do not match.\nPlease try again.\n";
        detail::msgbox_widget(PassErrBody);
        tui::set_root_password();
    }

#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    std::error_code err{};
    utils::exec(fmt::format(FMT_COMPILE("echo -e \"{}\n{}\" > /tmp/.passwd"), pass, confirm));
    utils::exec(fmt::format(FMT_COMPILE("arch-chroot {} passwd root < /tmp/.passwd &>/dev/null"), mountpoint));
    fs::remove("/tmp/.passwd", err);
#endif
}

// Create user on the system
void create_new_user() noexcept {
    std::string user{};
    static constexpr auto user_body = "Enter the user name. Letters MUST be lower case.";
    if (!detail::inputbox_widget(user, user_body, size(HEIGHT, GREATER_THAN, 1))) {
        return;
    }

    // Loop while username is blank, has spaces, or has capital letters in it.
    while (user.empty() || (user.find_first_of(" ") != std::string::npos) || ranges::any_of(user, [](char ch) { return std::isupper(ch); })) {
        user.clear();
        static constexpr auto user_err_body = "An incorrect user name was entered. Please try again.";
        if (!detail::inputbox_widget(user, user_err_body, size(HEIGHT, GREATER_THAN, 1))) {
            return;
        }
    }

#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
#endif

    std::string_view shell{};
    {
        static constexpr auto DefShell               = "\nChoose the default shell.\n";
        static constexpr auto UseSpaceBar            = "Use [Spacebar] to de/select options listed.";
        const auto& shells_options_body              = fmt::format(FMT_COMPILE("\n{}{}\n"), DefShell, UseSpaceBar);
        const std::vector<std::string> radiobox_list = {
            "zsh",
            "bash",
            "fish",
        };

        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        auto ok_callback = [&] {
            switch (selected) {
            case 0:
                shell = "/usr/bin/zsh";
                break;
            case 1:
                shell = "/bin/bash";
                break;
            case 2: {
                shell = "/usr/bin/fish";

#ifdef NDEVENV
                std::string_view packages{"cachyos-fish-config"};
                const auto& hostcache = std::get<std::int32_t>(config_data["hostcache"]);
                if (hostcache) {
                    detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("pacstrap {} {} |& tee /tmp/pacstrap.log"), mountpoint, packages)});
                    break;
                }
                detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("pacstrap -c {} {} |& tee /tmp/pacstrap.log"), mountpoint, packages)});
#endif
                break;
            }
            }
            screen.ExitLoopClosure()();
        };
        detail::radiolist_widget(radiobox_list, ok_callback, &selected, &screen, shells_options_body, detail::WidgetBoxSize{.text_size = nothing});
    }

    spdlog::info("default shell: [{}]", shell);

    // Enter password. This step will only be reached where the loop has been skipped or broken.
    std::string pass{};
    static constexpr auto user_pass_body = "Enter password for";
    if (!detail::inputbox_widget(pass, fmt::format(FMT_COMPILE("{} {}"), user_pass_body, user), size(HEIGHT, GREATER_THAN, 1), true)) {
        return;
    }
    std::string confirm{};
    static constexpr auto user_confirm_body = "Re-enter the password.";
    if (!detail::inputbox_widget(confirm, user_confirm_body, size(HEIGHT, GREATER_THAN, 1), true)) {
        return;
    }

    while (pass != confirm) {
        static constexpr auto PassErrBody = "\nThe passwords entered do not match.\nPlease try again.\n";
        detail::msgbox_widget(PassErrBody);
        pass.clear();
        confirm.clear();
        if (!detail::inputbox_widget(pass, fmt::format(FMT_COMPILE("{} {}"), user_pass_body, user), size(HEIGHT, GREATER_THAN, 1), true)) {
            return;
        }
        if (!detail::inputbox_widget(confirm, user_confirm_body, size(HEIGHT, GREATER_THAN, 1), true)) {
            return;
        }
    }

    // create new user. This step will only be reached where the password loop has been skipped or broken.
    detail::infobox_widget("\nCreating User and setting groups...\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));

#ifdef NDEVENV
    // Create the user, set password, then remove temporary password file
    utils::arch_chroot(fmt::format(FMT_COMPILE("groupadd {}"), user), false);
    utils::arch_chroot(fmt::format(FMT_COMPILE("useradd {0} -m -g {0} -G sudo,storage,power,network,video,audio,lp,sys,input -s {1}"), user, shell), false);
    spdlog::info("add user to groups");

    // check if user has been created
    const auto& user_check = utils::exec(fmt::format(FMT_COMPILE("arch-chroot {} getent passwd {}"), mountpoint, user));
    if (user_check.empty()) {
        spdlog::error("User has not been created!");
    }
    std::error_code err{};
    utils::exec(fmt::format(FMT_COMPILE("echo -e \"{}\\n{}\" > /tmp/.passwd"), pass, confirm));
    const auto& ret_status = utils::exec(fmt::format(FMT_COMPILE("arch-chroot {} passwd {} < /tmp/.passwd &>/dev/null"), mountpoint, user), true);
    spdlog::info("create user pwd: {}", ret_status);
    fs::remove("/tmp/.passwd", err);

    // Set up basic configuration files and permissions for user
    // arch_chroot "cp /etc/skel/.bashrc /home/${USER}"
    utils::arch_chroot(fmt::format(FMT_COMPILE("chown -R {0}:{0} /home/{0}"), user), false);
    const auto& sudoers_file = fmt::format(FMT_COMPILE("{}/etc/sudoers"), mountpoint);
    if (fs::exists(sudoers_file)) {
        utils::exec(fmt::format(FMT_COMPILE("sed -i '/NOPASSWD/!s/# %sudo/%sudo/g' {}"), sudoers_file));
    }
#endif
}

// Install pkgs from user input
void install_cust_pkgs() noexcept {
    std::string packages{};
    static constexpr auto content = "\nType any extra packages you would like to add, separated by spaces.\n \nFor example, to install Firefox, MPV, FZF:\nfirefox mpv fzf\n";
    if (!detail::inputbox_widget(packages, content, size(HEIGHT, GREATER_THAN, 4))) {
        return;
    }
    // If at least one package, install.
    /* clang-format off */
    if (packages.empty()) { return; }
    /* clang-format on */

#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& hostcache  = std::get<std::int32_t>(config_data["hostcache"]);

    if (hostcache) {
        // utils::exec(fmt::format(FMT_COMPILE("pacstrap {} {}"), mountpoint, packages));
        detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("pacstrap {} {}"), mountpoint, packages)});
        return;
    }
    // utils::exec(fmt::format(FMT_COMPILE("pacstrap -c {} {}"), mountpoint, packages));
    detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("pacstrap -c {} {}"), mountpoint, packages)});
#endif
}

void rm_pgs() noexcept {
    std::string packages{};
    static constexpr auto content = "\nType any packages you would like to remove, separated by spaces.\n \nFor example, to remove Firefox, MPV, FZF:\nfirefox mpv fzf\n";
    if (!detail::inputbox_widget(packages, content, size(HEIGHT, GREATER_THAN, 4))) {
        return;
    }
    /* clang-format off */
    if (packages.empty()) { return; }
    /* clang-format on */

#ifdef NDEVENV
    utils::arch_chroot(fmt::format(FMT_COMPILE("pacman -Rsn {}"), packages));
#endif
}

void chroot_interactive() noexcept {
    static constexpr auto chroot_return = "\nYou will now chroot into your installed system.\nYou can do changes almost as if you had booted into your installation.\n \nType \"exit\" to exit chroot.\n";
    detail::infobox_widget(chroot_return);
    std::this_thread::sleep_for(std::chrono::seconds(1));

#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    const auto& cmd_formatted = fmt::format(FMT_COMPILE("arch-chroot {} bash"), mountpoint);
    utils::exec(cmd_formatted, true);
#else
    utils::exec("bash", true);
#endif
}

void install_grub_uefi() noexcept {
    static constexpr auto content = "\nInstall UEFI Bootloader GRUB.\n";
    const auto& do_install_uefi   = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    /* clang-format off */
    if (!do_install_uefi) { return; }
    /* clang-format on */

    std::string bootid{"cachyos"};
    auto ret_status = utils::exec("efibootmgr | cut -d\\  -f2 | grep -q -o cachyos", true);
    if (ret_status == "0") {
        static constexpr auto bootid_content = "\nInput the name identify your grub installation. Choosing an existing name overwrites it.\n";
        if (!detail::inputbox_widget(bootid, bootid_content, size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
            return;
        }
    }

    utils::clear_screen();
#ifdef NDEVENV
    fs::create_directory("/mnt/hostlvm");
    utils::exec("mount --bind /run/lvm /mnt/hostlvm");
#endif

    // if root is encrypted, amend /etc/default/grub
    const auto& root_name   = utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    const auto& root_device = utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/disk/p\" | {}"), root_name, "awk '/disk/ {print $1}'"));
    const auto& root_part   = utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/part/p\" | {} | tr -cd '[:alnum:]'"), root_name, "awk '/part/ {print $1}'"));
#ifdef NDEVENV
    utils::boot_encrypted_setting();
#endif

    spdlog::info("root_name: {}. root_device: {}. root_part: {}", root_name, root_device, root_part);

    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& uefi_mount = std::get<std::string>(config_data["UEFI_MOUNT"]);

#ifdef NDEVENV
    const auto& mountpoint          = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& grub_installer_path = fmt::format(FMT_COMPILE("{}/usr/bin/grub_installer.sh"), mountpoint);

    // grub config changes for non zfs root
    if (utils::exec(fmt::format(FMT_COMPILE("findmnt -ln -o FSTYPE \"{}\""), mountpoint)) == "zfs") {
        return;
    }
    {
        constexpr auto bash_codepart = R"(#!/bin/bash
ln -s /hostlvm /run/lvm
pacman -S --noconfirm --needed grub efibootmgr dosfstools grub-btrfs
findmnt | awk '/^\/ / {print $3}' | grep -q btrfs && sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i /etc/default/grub
lsblk -ino TYPE,MOUNTPOINT | grep " /$" | grep -q lvm && sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i /etc/default/grub)";

        static constexpr auto mkconfig_codepart = "grub-mkconfig -o /boot/grub/grub.cfg";
        const auto& bash_code                   = fmt::format(FMT_COMPILE("{}\ngrub-install --target=x86_64-efi --efi-directory={} --bootloader-id={} --recheck\n{}\n"), bash_codepart, uefi_mount, bootid, mkconfig_codepart);
        std::ofstream grub_installer{grub_installer_path};
        grub_installer << bash_code;
    }

    fs::permissions(grub_installer_path,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add);

    // if the device is removable append removable to the grub-install
    const auto& removable = utils::exec(fmt::format(FMT_COMPILE("cat /sys/block/{}/removable"), root_device));
    if (utils::to_int(removable.data()) == 1) {
        utils::exec(fmt::format(FMT_COMPILE("sed -e '/^grub-install /s/$/ --removable/g' -i {}"), grub_installer_path));
    }

    // If the root is on btrfs-subvolume, amend grub installation
    ret_status = utils::exec("mount | awk '$3 == \"/mnt\" {print $0}' | grep btrfs | grep -qv subvolid=5", true);
    if (ret_status != "0") {
        utils::exec(fmt::format(FMT_COMPILE("sed -e 's/ grub-btrfs//g' -i {}"), grub_installer_path));
    }

    // If Full disk encryption is used, use a keyfile
    const auto& fde = std::get<std::int32_t>(config_data["fde"]);
    if (fde == 1) {
        spdlog::info("Full disk encryption enabled");
        utils::exec(fmt::format(FMT_COMPILE("sed '3a\\grep -q \"^GRUB_ENABLE_CRYPTODISK=y\" /etc/default/grub || sed -i \"s/#GRUB_ENABLE_CRYPTODISK=y/GRUB_ENABLE_CRYPTODISK=y/\" /etc/default/grub' -i {}"), grub_installer_path));
    }

    std::error_code err{};

    // install grub
    utils::arch_chroot("grub_installer.sh");
    umount("/mnt/hostlvm");
    fs::remove("/mnt/hostlvm", err);

    // the grub_installer is no longer needed
    fs::remove(grub_installer_path, err);
#endif
    // Ask if user wishes to set Grub as the default bootloader and act accordingly
    static constexpr auto set_boot_default_body  = "Some UEFI firmware may not detect the bootloader unless it is set\nas default by copying its efi stub to";
    static constexpr auto set_boot_default_body2 = "and renaming it to bootx64.efi.\n\nIt is recommended to do so unless already using a default bootloader,\nor where intending to use multiple bootloaders.\n\nSet bootloader as default?";

    const auto& do_set_default_bootloader = detail::yesno_widget(fmt::format(FMT_COMPILE("\n{} {}/EFI/boot {}\n"), set_boot_default_body, uefi_mount, set_boot_default_body2), size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    /* clang-format off */
    if (!do_set_default_bootloader) { return; }
    /* clang-format on */

#ifdef NDEVENV
    utils::arch_chroot(fmt::format(FMT_COMPILE("mkdir {}/EFI/boot"), uefi_mount));
    spdlog::info("Grub efi binary status:(EFI/cachyos/grubx64.efi): {}", fs::exists(fmt::format(FMT_COMPILE("{0}/EFI/cachyos/grubx64.efi"), uefi_mount)));
    utils::arch_chroot(fmt::format(FMT_COMPILE("cp -r {0}/EFI/cachyos/grubx64.efi {0}/EFI/boot/bootx64.efi"), uefi_mount));
#endif

    detail::infobox_widget("\nGrub has been set as the default bootloader.\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

void install_systemd_boot() noexcept {
    static constexpr auto content = "\nThis installs systemd-boot and generates boot entries\nfor the currently installed kernels.\nThis bootloader requires your kernels to be on the UEFI partition.\nThis is achieved by mounting the UEFI partition to /boot.\n";
    const auto& do_install_uefi   = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    /* clang-format off */
    if (!do_install_uefi) { return; }
    /* clang-format on */

#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& uefi_mount = std::get<std::string>(config_data["UEFI_MOUNT"]);

    utils::arch_chroot(fmt::format(FMT_COMPILE("bootctl --path={} install"), uefi_mount), false);
    // utils::exec(fmt::format(FMT_COMPILE("pacstrap {} systemd-boot-manager"), mountpoint));
    detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("pacstrap {} systemd-boot-manager"), mountpoint)});
    utils::arch_chroot("sdboot-manage gen", false);

    // Check if the volume is removable. If so, dont use autodetect
    const auto& root_name   = utils::exec("mount | awk \'/\\/mnt / {print $1}\' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    const auto& root_device = utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r \'s/^[^[:alnum:]]+//\' | sed -n -e \"/{}/,/disk/p\" | {}"), root_name, "awk \'/disk/ {print $1}\'"));
    spdlog::info("root_name: {}. root_device: {}", root_name, root_device);
    const auto& removable = utils::exec(fmt::format(FMT_COMPILE("cat /sys/block/{}/removable"), root_device));
    if (utils::to_int(removable.data()) == 1) {
        // Remove autodetect hook
        utils::exec("sed -i -e \'/^HOOKS=/s/\\ autodetect//g\' /mnt/etc/mkinitcpio.conf");
        spdlog::info("\"Autodetect\" hook was removed");
    }
#endif
    spdlog::info("Systemd-boot was installed");
    detail::infobox_widget("\nSystemd-boot was installed\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

void uefi_bootloader() noexcept {
    // Ensure again that efivarfs is mounted
    static constexpr auto efi_path = "/sys/firmware/efi/";
    if (fs::exists(efi_path) && fs::is_directory(efi_path)) {
        // Mount efivarfs if it is not already mounted
        const auto& mount_out = utils::exec("mount | grep /sys/firmware/efi/efivars");
        if (mount_out.empty()) {
            if (mount("efivarfs", "/sys/firmware/efi/efivars", "efivarfs", 0, "") != 0) {
                perror("utils::uefi_bootloader");
                exit(1);
            }
        }
    }

    static constexpr auto bootloaderInfo        = "Refind can be used standalone or in conjunction with other bootloaders as a graphical bootmenu.\nIt autodetects all bootable systems at boot time.\nGrub supports encrypted /boot partition and detects all bootable systems when you update your kernels.\nIt supports booting .iso files from a harddrive and automatic boot entries for btrfs snapshots.\nSystemd-boot is very light and simple and has little automation.\nIt autodetects windows, but is otherwise unsuited for multibooting.";
    const std::vector<std::string> menu_entries = {
        "grub",
        "refind",
        "systemd-boot",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            tui::install_grub_uefi();
            break;
        case 1:
            // tui::install_refind();
            spdlog::debug("refind");
            break;
        case 2:
            tui::install_systemd_boot();
            break;
        }
        screen.ExitLoopClosure()();
    };

    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, bootloaderInfo);
}

void install_base() noexcept {
    static constexpr auto base_installed = "/mnt/.base_installed";
    if (fs::exists(base_installed)) {
        static constexpr auto content = "\nA CachyOS Base has already been installed on this partition.\nProceed anyway?\n";
        const auto& do_reinstall      = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
        /* clang-format off */
        if (!do_reinstall) { return; }
        /* clang-format on */
        std::error_code err{};
        fs::remove(base_installed, err);
    }
    // Prep variables
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const std::vector<std::string> available_kernels{"linux-cachyos", "linux", "linux-zen", "linux-lts", "linux-cachyos-cacule", "linux-cachyos-bmq", "linux-cachyos-pds", "linux-cachyos-tt", "linux-cachyos-bore"};

    // Create the base list of packages
    std::vector<std::string> install_packages{};
    std::unique_ptr<bool[]> kernels_state{new bool[available_kernels.size()]{false}};

    auto screen = ScreenInteractive::Fullscreen();
    std::string packages{};
    auto ok_callback = [&] {
        packages        = detail::from_checklist_string(available_kernels, kernels_state.get());
        auto ret_status = utils::exec(fmt::format(FMT_COMPILE("echo \"{}\" | grep \"linux\""), packages), true);
        if (ret_status != "0") {
            // Check if a kernel is already installed
            ret_status = utils::exec(fmt::format(FMT_COMPILE("ls {}/boot/*.img >/dev/null 2>&1"), mountpoint), true);
            if (ret_status != "0") {
                static constexpr auto ErrNoKernel = "\nAt least one kernel must be selected.\n";
                detail::msgbox_widget(ErrNoKernel);
                return;
            }
            const auto& cmd = utils::exec(fmt::format(FMT_COMPILE("ls {}/boot/*.img | cut -d'-' -f2 | grep -v ucode.img | sort -u"), mountpoint));
            detail::msgbox_widget(fmt::format(FMT_COMPILE("\nlinux-{} detected\n"), cmd));
            screen.ExitLoopClosure()();
        }
        spdlog::info("selected: {}", packages);
        screen.ExitLoopClosure()();
    };

    static constexpr auto InstStandBseBody = "\nThe base package group will be installed automatically.\nThe base-devel package group is required to use the Arch User Repository (AUR).\n";
    static constexpr auto UseSpaceBar      = "Use [Spacebar] to de/select options listed.";
    const auto& kernels_options_body       = fmt::format(FMT_COMPILE("\n{}{}\n"), InstStandBseBody, UseSpaceBar);

    constexpr auto base_title = "New CLI Installer | Install Base";
    detail::checklist_widget(available_kernels, ok_callback, kernels_state.get(), &screen, kernels_options_body, base_title, {.text_size = nothing});

    /* clang-format off */
    if (packages.empty()) { return; }
    /* clang-format on */

    auto pkg_list = utils::make_multiline(packages, false, " ");

    const auto pkg_count = pkg_list.size();
    for (std::size_t i = 0; i < pkg_count; ++i) {
        const auto& pkg = pkg_list[i];
        pkg_list.emplace_back(fmt::format(FMT_COMPILE("{}-headers"), pkg));
    }
    pkg_list.insert(pkg_list.cend(), {"amd-ucode", "intel-ucode"});
    pkg_list.insert(pkg_list.cend(), {"base", "base-devel", "zsh", "mhwd-cachyos", "vim"});
    pkg_list.insert(pkg_list.cend(), {"cachyos-keyring", "cachyos-mirrorlist", "cachyos-v3-mirrorlist", "cachyos-hello", "cachyos-hooks", "cachyos-settings", "cachyos-rate-mirrors", "cachy-browser"});
    packages = utils::make_multiline(pkg_list, false, " ");

    spdlog::info(fmt::format("Preparing for pkgs to install: \"{}\"", packages));

#ifdef NDEVENV
    // filter_packages
    const auto& hostcache = std::get<std::int32_t>(config_data["hostcache"]);
    if (hostcache) {
        detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("pacstrap {} {} |& tee /tmp/pacstrap.log"), mountpoint, packages)});
    } else {
        detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("pacstrap -c {} {} |& tee /tmp/pacstrap.log"), mountpoint, packages)});
    }

    fs::copy_file("/etc/pacman.conf", fmt::format(FMT_COMPILE("{}/etc/pacman.conf"), mountpoint), fs::copy_options::overwrite_existing);
    std::ofstream{base_installed};

    // mkinitcpio handling for specific filesystems
    std::int32_t btrfs_root = 0;
    std::int32_t zfs_root   = 0;

    const auto& filesystem_type = fmt::format(FMT_COMPILE("findmnt -ln -o FSTYPE {}"), mountpoint);
    if (filesystem_type == "btrfs") {
        btrfs_root = 1;
        utils::exec(fmt::format(FMT_COMPILE("sed -e '/^HOOKS=/s/\\ fsck//g' -e '/^MODULES=/s/\"$/ btrfs\"/g' -i {}/etc/mkinitcpio.conf"), mountpoint));
    } else if (filesystem_type == "zfs") {
        zfs_root = 1;
        utils::exec(fmt::format(FMT_COMPILE("sed -e '/^HOOKS=/s/\\ filesystems//g' -e '/^HOOKS=/s/\\ keyboard/\\ keyboard\\ zfs\\ filesystems/g' -e '/^HOOKS=/s/\\ fsck//g' -e '/^FILES=/c\\FILES=(\"/usr/lib/libgcc_s.so.1\")' -i {}/etc/mkinitcpio.conf"), mountpoint));
    }

    // add luks and lvm hooks as needed
    const auto& lvm  = std::get<std::int32_t>(config_data["LVM"]);
    const auto& luks = std::get<std::int32_t>(config_data["LUKS"]);

    if (lvm == 1 && luks == 0) {
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/block filesystems/block lvm2 filesystems/g' {}/etc/mkinitcpio.conf"), mountpoint));
    } else if (lvm == 0 && luks == 1) {
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/block filesystems keyboard/block consolefont keymap keyboard encrypt filesystems/g' {}/etc/mkinitcpio.conf"), mountpoint));
    } else if (lvm == 1 && luks == 1) {
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/block filesystems keyboard/block consolefont keymap keyboard encrypt lvm2 filesystems/g' {}/etc/mkinitcpio.conf"), mountpoint));
    }

    if (lvm + luks + btrfs_root + zfs_root > 0) {
        utils::arch_chroot("mkinitcpio -P");
    }

    // Generate fstab with UUID
    utils::exec(fmt::format(FMT_COMPILE("genfstab -U -p {0} > {0}/etc/fstab"), mountpoint));
    // Edit fstab in case of btrfs subvolumes
    utils::exec(fmt::format(FMT_COMPILE("sed -i \"s/subvolid=.*,subvol=\\/.*,//g\" {}/etc/fstab"), mountpoint));
#endif
}

void install_desktop() noexcept {
#ifdef NDEVENV
    static constexpr auto base_installed = "/mnt/.base_installed";
    if (!fs::exists(base_installed)) {
        static constexpr auto content = "\nA CachyOS Base is not installed on this partition.\n";
        detail::infobox_widget(content);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return;
    }
#endif

    // Prep variables
    const std::vector<std::string> available_des{"kde", "cutefish", "xfce", "sway", "wayfire", "i3wm", "openbox"};

    // Create the base list of packages
    std::vector<std::string> install_packages{};
    std::unique_ptr<bool[]> des_state{new bool[available_des.size()]{false}};

    auto screen = ScreenInteractive::Fullscreen();
    std::string desktop_env{};
    auto ok_callback = [&] {
        desktop_env = detail::from_checklist_string(available_des, des_state.get());
        spdlog::info("selected: {}", desktop_env);
        screen.ExitLoopClosure()();
    };

    static constexpr auto InstManDEBody = "\nPlease choose a desktop environment.\n";
    static constexpr auto UseSpaceBar   = "Use [Spacebar] to de/select options listed.";
    const auto& des_options_body        = fmt::format(FMT_COMPILE("\n{}{}\n"), InstManDEBody, UseSpaceBar);

    constexpr auto desktop_title = "New CLI Installer | Install Desktop";
    detail::checklist_widget(available_des, ok_callback, des_state.get(), &screen, des_options_body, desktop_title, {.text_size = nothing});

    /* clang-format off */
    if (desktop_env.empty()) { return; }
    /* clang-format on */

    std::vector<std::string> pkg_list{};

    constexpr std::string_view kde{"kde"};
    constexpr std::string_view sway{"sway"};
    constexpr std::string_view i3wm{"i3wm"};
    constexpr std::string_view xfce{"xfce"};
    constexpr std::string_view cutefish{"cutefish"};
    constexpr std::string_view wayfire{"wayfire"};
    constexpr std::string_view openbox{"openbox"};

    bool needed_xorg{};
    auto found = ranges::search(desktop_env, i3wm);
    if (!found.empty()) {
        pkg_list.insert(pkg_list.cend(), {"i3-wm", "i3blocks", "i3lock", "i3status"});
        needed_xorg = true;
    }
    found = ranges::search(desktop_env, sway);
    if (!found.empty()) {
        pkg_list.insert(pkg_list.cend(), {"sway", "waybar"});
    }
    found = ranges::search(desktop_env, kde);
    if (!found.empty()) {
        /* clang-format off */
        static constexpr std::array to_be_inserted{"plasma-desktop", "plasma-framework", "plasma-nm", "plasma-pa", "plasma-workspace",
            "konsole", "kate", "dolphin", "sddm", "sddm-kcm", "plasma", "plasma-wayland-protocols", "plasma-wayland-session",
            "gamemode", "lib32-gamemode", "ksysguard", "pamac-aur", "openssh", "btop"};
        /* clang-format on */
        pkg_list.insert(pkg_list.end(), std::move_iterator(to_be_inserted.begin()),
            std::move_iterator(to_be_inserted.end()));
        needed_xorg = true;
    }
    found = ranges::search(desktop_env, xfce);
    if (!found.empty()) {
        /* clang-format off */
        static constexpr std::array to_be_inserted{"file-roller", "galculator", "gvfs", "gvfs-afc", "gvfs-gphoto2", "gvfs-mtp", "gvfs-nfs", "gvfs-smb", "lightdm", "lightdm-gtk-greeter", "lightdm-gtk-greeter-settings", "network-manager-applet", "parole", "ristretto", "thunar-archive-plugin", "thunar-media-tags-plugin", "xdg-user-dirs-gtk", "xed", "xfce4", "xfce4-battery-plugin", "xfce4-datetime-plugin", "xfce4-mount-plugin", "xfce4-netload-plugin", "xfce4-notifyd", "xfce4-pulseaudio-plugin", "xfce4-screensaver", "xfce4-screenshooter", "xfce4-taskmanager", "xfce4-wavelan-plugin", "xfce4-weather-plugin", "xfce4-whiskermenu-plugin", "xfce4-xkb-plugin"};
        /* clang-format on */
        pkg_list.insert(pkg_list.end(), std::move_iterator(to_be_inserted.begin()),
            std::move_iterator(to_be_inserted.end()));
        needed_xorg = true;
    }
    found = ranges::search(desktop_env, cutefish);
    if (!found.empty()) {
        pkg_list.insert(pkg_list.cend(), {"cutefish", "fish-ui"});
        needed_xorg = true;
    }
    found = ranges::search(desktop_env, wayfire);
    if (!found.empty()) {
        pkg_list.insert(pkg_list.cend(), {"wayfire", "wayfire-plugins-extra", "wf-config", "wf-shell", "wf-recorder", "nwg-drawer"});
    }
    found = ranges::search(desktop_env, openbox);
    if (!found.empty()) {
        pkg_list.insert(pkg_list.cend(), {"openbox", "obconf"});
    }

    if (needed_xorg) {
        pkg_list.insert(pkg_list.cend(), {"libwnck3", "xf86-input-libinput", "xf86-video-fbdev", "xf86-video-vesa", "xorg-server", "xorg-xinit", "xorg-xinput", "xorg-xkill", "xorg-xrandr", "xf86-video-amdgpu", "xf86-video-ati", "xf86-video-intel"});
    }

    const std::string packages = utils::make_multiline(pkg_list, false, " ");

    spdlog::info(fmt::format("Preparing for desktop envs to install: \"{}\"", packages));
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& hostcache  = std::get<std::int32_t>(config_data["hostcache"]);

    if (hostcache) {
        detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("pacstrap {} {}"), mountpoint, packages)});
        return;
    }
    detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("pacstrap -c {} {}"), mountpoint, packages)});
#endif
}

// Base Configuration
void config_base_menu() noexcept {
    const std::vector<std::string> menu_entries = {
        "Generate FSTAB",
        "Set Hostname",
        "Set System Locale",
        "Set Desktop Keyboard Layout",
        "Set Timezone and Clock",
        "Set Root Password",
        "Add New User(s)",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            tui::generate_fstab();
            break;
        case 1:
            tui::set_hostname();
            break;
        case 2:
            tui::set_locale();
            break;
        case 3:
            tui::set_xkbmap();
            break;
        case 4: {
            if (!tui::set_timezone()) {
                screen.ExitLoopClosure()();
                break;
            }
            tui::set_hw_clock();
            break;
        }
        case 5:
            tui::set_root_password();
            break;
        case 6:
            tui::create_new_user();
            break;
        default:
            screen.ExitLoopClosure()();
            break;
        }
    };

    static constexpr auto config_base_body = "Basic configuration of the base.";
    const auto& content_size               = size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, config_base_body, {content_size, size(ftxui::HEIGHT, ftxui::GREATER_THAN, 1)});
}

// Grub auto-detects installed kernel
void bios_bootloader() {
    static constexpr auto bootloaderInfo        = "The installation device for GRUB can be selected in the next step.\n \nos-prober is needed for automatic detection of already installed\nsystems on other partitions.";
    const std::vector<std::string> menu_entries = {
        "grub",
        "grub + os-prober",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    std::string selected_bootloader{};
    auto ok_callback = [&] {
        selected_bootloader = menu_entries[static_cast<std::size_t>(selected)];
        screen.ExitLoopClosure()();
    };
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, bootloaderInfo);

    /* clang-format off */
    if (selected_bootloader.empty()) { return; }
    /* clang-format on */

    selected_bootloader = utils::exec(fmt::format(FMT_COMPILE("echo \"{}\" | sed 's/+ \\|\"//g'"), selected_bootloader));

    if (!tui::select_device()) {
        return;
    }
#ifdef NDEVENV
    // if root is encrypted, amend /etc/default/grub
    utils::boot_encrypted_setting();

    auto* config_instance    = Config::instance();
    auto& config_data        = config_instance->data();
    const auto& lvm          = std::get<std::int32_t>(config_data["LVM"]);
    const auto& lvm_sep_boot = std::get<std::int32_t>(config_data["LVM_SEP_BOOT"]);
    const auto& luks_dev     = std::get<std::string>(config_data["LUKS_DEV"]);
    const auto& mountpoint   = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& device_info  = std::get<std::string>(config_data["DEVICE"]);

    // if /boot is LVM (whether using a seperate /boot mount or not), amend grub
    if ((lvm == 1 && lvm_sep_boot == 0) || lvm_sep_boot == 2) {
        utils::exec(fmt::format(FMT_COMPILE("sed -i \"s/GRUB_PRELOAD_MODULES=\\\"/GRUB_PRELOAD_MODULES=\\\"lvm /g\" {}/etc/default/grub"), mountpoint));
        utils::exec(fmt::format(FMT_COMPILE("sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i {}/etc/default/grub"), mountpoint));
    }

    // If root is on btrfs volume, amend grub
    if (utils::exec(fmt::format(FMT_COMPILE("findmnt -no FSTYPE {}"), mountpoint)) == "btrfs") {
        utils::exec(fmt::format(FMT_COMPILE("sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i {}/etc/default/grub"), mountpoint));
    }

    // Same setting is needed for LVM
    if (lvm == 1) {
        utils::exec(fmt::format(FMT_COMPILE("sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i {}/etc/default/grub"), mountpoint));
    }

    // grub config changes for non zfs root
    if (utils::exec(fmt::format(FMT_COMPILE("findmnt -ln -o FSTYPE \"{}\""), mountpoint)) == "zfs") {
        return;
    }

    const auto& grub_installer_path = fmt::format(FMT_COMPILE("{}/usr/bin/grub_installer.sh"), mountpoint);
    {
        constexpr auto bash_codepart = R"(#!/bin/bash
ln -s /hostlvm /run/lvm
pacman -S --noconfirm --needed grub os-prober grub-btrfs
findmnt | awk '/^\/ / {print $3}' | grep -q btrfs && sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i /etc/default/grub
grub-install --target=i386-pc --recheck
grub-mkconfig -o /boot/grub/grub.cfg)";

        const auto& bash_code = fmt::format(FMT_COMPILE("{} {}\n"), bash_codepart, device_info);
        std::ofstream grub_installer{grub_installer_path};
        grub_installer << bash_code;
    }

    // If the root is on btrfs-subvolume, amend grub installation
    auto ret_status = utils::exec("mount | awk '$3 == \"/mnt\" {print $0}' | grep btrfs | grep -qv subvolid=5", true);
    if (ret_status != "0") {
        utils::exec(fmt::format(FMT_COMPILE("sed -e 's/ grub-btrfs//g' -i {}"), grub_installer_path));
    }

    // If encryption used amend grub
    if (luks_dev != "") {
        utils::exec(fmt::format(FMT_COMPILE("sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i {}/etc/default/grub"), mountpoint));

        const auto& luks_dev_formatted = utils::exec(fmt::format(FMT_COMPILE("echo \"{}\" | {}"), luks_dev, "awk '{print $1}'"));
        ret_status                     = utils::exec(fmt::format(FMT_COMPILE("echo \"sed -i \"s~GRUB_CMDLINE_LINUX=.*~GRUB_CMDLINE_LINUX=\\\"\"{}\\\"~g\"\" /etc/default/grub\" >> {}"), luks_dev_formatted, grub_installer_path), true);
        if (ret_status == "0") {
            spdlog::info("adding kernel parameter {}", luks_dev);
        }
    }

    // If Full disk encryption is used, use a keyfile
    const auto& fde = std::get<std::int32_t>(config_data["fde"]);
    if (fde == 1) {
        spdlog::info("Full disk encryption enabled");
        utils::exec(fmt::format(FMT_COMPILE("sed '3a\\grep -q \"^GRUB_ENABLE_CRYPTODISK=y\" /etc/default/grub || sed -i \"s/#GRUB_ENABLE_CRYPTODISK=y/GRUB_ENABLE_CRYPTODISK=y/\" /etc/default/grub' -i {}"), grub_installer_path));
    }

    // Remove os-prober if not selected
    constexpr std::string_view needle{"os-prober"};
    const auto& found = ranges::search(selected_bootloader, needle);
    if (found.empty()) {
        utils::exec(fmt::format(FMT_COMPILE("sed -e 's/ os-prober//g' -i {}"), grub_installer_path));
    }

    std::error_code err{};

    fs::permissions(grub_installer_path,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add);

    detail::infobox_widget("\nPlease wait...\n");
    utils::exec(fmt::format(FMT_COMPILE("dd if=/dev/zero of={} seek=1 count=2047"), device_info));
    fs::create_directory("/mnt/hostlvm", err);
    utils::exec("mount --bind /run/lvm /mnt/hostlvm");

    // install grub
    utils::arch_chroot("grub_installer.sh");

    // the grub_installer is no longer needed - there still needs to be a better way to do this
    fs::remove(grub_installer_path, err);

    umount("/mnt/hostlvm");
    fs::remove("/mnt/hostlvm", err);
#endif
}

void enable_autologin() {
    // Detect display manager
    const auto& dm      = utils::exec("file /mnt/etc/systemd/system/display-manager.service 2>/dev/null | awk -F'/' '{print $NF}' | cut -d. -f1");
    const auto& content = fmt::format(FMT_COMPILE("\nThis option enables autologin using {}.\n\nProceed?\n"), dm);
    /* clang-format off */
    if (!detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75))) { return; }
    if (utils::exec("echo /mnt/home/* | xargs -n1 | wc -l") != "1") { return; }
    /* clang-format on */

    const auto& autologin_user = utils::exec("echo /mnt/home/* | cut -d/ -f4");
    utils::enable_autologin(dm, autologin_user);
}

void performance_menu() {
    const std::vector<std::string> menu_entries = {
        "I/O Schedulers",
        "Swap Configuration",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            screen.Suspend();
            utils::set_schedulers();
            screen.Resume();
            break;
        case 1:
            screen.Suspend();
            utils::set_swappiness();
            screen.Resume();
            break;
        default:
            screen.ExitLoopClosure()();
            break;
        }
    };
    static constexpr auto tweaks_body = "Various configuration options";
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, tweaks_body, {.text_size = size(HEIGHT, GREATER_THAN, 1)});
}

void tweaks_menu() noexcept {
    const std::vector<std::string> menu_entries = {
        "Enable Automatic Login",
        "Performance",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            tui::enable_autologin();
            break;
        case 1:
            tui::performance_menu();
            break;
        default:
            screen.ExitLoopClosure()();
            break;
        }
    };
    static constexpr auto tweaks_body = "Various configuration options";
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, tweaks_body, {.text_size = size(HEIGHT, GREATER_THAN, 1)});
}

void install_bootloader() noexcept {
    /* clang-format off */
    if (!utils::check_base()) { return; }
    /* clang-format on */

    auto* config_instance   = Config::instance();
    auto& config_data       = config_instance->data();
    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    if (system_info == "BIOS")
        bios_bootloader();
    else
        uefi_bootloader();
}

// BIOS and UEFI
void auto_partition() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& device_info                  = std::get<std::string>(config_data["DEVICE"]);
    [[maybe_unused]] const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);

    // Find existing partitions (if any) to remove
    const auto& parts     = utils::exec(fmt::format(FMT_COMPILE("parted -s {} print | {}"), device_info, "awk \'/^ / {print $1}\'"));
    const auto& del_parts = utils::make_multiline(parts);
    for (const auto& del_part : del_parts) {
#ifdef NDEVENV
        utils::exec(fmt::format(FMT_COMPILE("parted -s {} rm {} &>/dev/null"), device_info, del_part));
#else
        spdlog::debug("{}", del_part);
#endif
    }

#ifdef NDEVENV
    // Identify the partition table
    const auto& part_table = utils::exec(fmt::format(FMT_COMPILE("parted -s {} print | grep -i \'partition table\' | {}"), device_info, "awk \'{print $3}\'"));

    // Create partition table if one does not already exist
    if ((system_info == "BIOS") && (part_table != "msdos"))
        utils::exec(fmt::format(FMT_COMPILE("parted -s {} mklabel msdos &>/dev/null"), device_info));
    if ((system_info == "UEFI") && (part_table != "gpt"))
        utils::exec(fmt::format(FMT_COMPILE("parted -s {} mklabel gpt &>/dev/null"), device_info));

    // Create partitions (same basic partitioning scheme for BIOS and UEFI)
    if (system_info == "BIOS")
        utils::exec(fmt::format(FMT_COMPILE("parted -s {} mkpart primary ext3 1MiB 513MiB &>/dev/null"), device_info));
    else
        utils::exec(fmt::format(FMT_COMPILE("parted -s {} mkpart ESP fat32 1MiB 513MiB &>/dev/null"), device_info));

    utils::exec(fmt::format(FMT_COMPILE("parted -s {} set 1 boot on &>/dev/null"), device_info));
    utils::exec(fmt::format(FMT_COMPILE("parted -s {} mkpart primary ext3 513MiB 100% &>/dev/null"), device_info));
#endif

    // Show created partitions
    const auto& disk_list = utils::exec(fmt::format(FMT_COMPILE("lsblk {} -o NAME,TYPE,FSTYPE,SIZE"), device_info));
    detail::msgbox_widget(disk_list, size(HEIGHT, GREATER_THAN, 5));
}

// Simple code to show devices / partitions.
void show_devices() noexcept {
    const auto& lsblk = utils::exec("lsblk -o NAME,MODEL,TYPE,FSTYPE,SIZE,MOUNTPOINT | grep \"disk\\|part\\|lvm\\|crypt\\|NAME\\|MODEL\\|TYPE\\|FSTYPE\\|SIZE\\|MOUNTPOINT\"");
    detail::msgbox_widget(lsblk, size(HEIGHT, GREATER_THAN, 5));
}

// Refresh pacman keys
void refresh_pacman_keys() noexcept {
#ifdef NDEVENV
    utils::arch_chroot("pacman-key --init;pacman-key --populate archlinux cachyos;pacman-key --refresh-keys;");
#else
    SPDLOG_DEBUG("({}) Function is doing nothing in dev environment!", __PRETTY_FUNCTION__);
#endif
}

// This function does not assume that the formatted device is the Root installation device as
// more than one device may be formatted. Root is set in the mount_partitions function.
bool select_device() noexcept {
    auto* config_instance    = Config::instance();
    auto& config_data        = config_instance->data();
    auto devices             = utils::exec("lsblk -lno NAME,SIZE,TYPE | grep 'disk' | awk '{print \"/dev/\" $1 \" \" $2}' | sort -u");
    const auto& devices_list = utils::make_multiline(devices);

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    bool success{};
    auto ok_callback = [&] {
        const auto& src       = devices_list[static_cast<std::size_t>(selected)];
        const auto& lines     = utils::make_multiline(src, false, " ");
        config_data["DEVICE"] = lines[0];
        success               = true;
        screen.ExitLoopClosure()();
    };
    detail::menu_widget(devices_list, ok_callback, &selected, &screen);

    return success;
}

// Set static list of filesystems rather than on-the-fly. Partially as most require additional flags, and
// partially because some don't seem to be viable.
// Set static list of filesystems rather than on-the-fly.
bool select_filesystem() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    // prep variables
    config_data["fs_opts"] = std::vector<std::string>{};

    const std::vector<std::string> menu_entries = {
        "Do not format -",
        "btrfs mkfs.btrfs -f",
        "ext2 mkfs.ext2 -q",
        "ext3 mkfs.ext3 -q",
        "ext4 mkfs.ext4 -q",
        "f2fs mkfs.f2fs -q",
        "jfs mkfs.jfs -q",
        "nilfs2 mkfs.nilfs2 -fq",
        "ntfs mkfs.ntfs -q",
        "reiserfs mkfs.reiserfs -q",
        "vfat mkfs.vfat -F32",
        "xfs mkfs.xfs -f",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    bool success{};
    auto ok_callback = [&] {
        const auto& src      = menu_entries[static_cast<std::size_t>(selected)];
        const auto& lines    = utils::make_multiline(src, false, " ");
        const auto& file_sys = lines[0];
        if (file_sys == "btrfs") {
            config_data["FILESYSTEM"] = "mkfs.btrfs -f";
            config_data["fs_opts"]    = std::vector<std::string>{"autodefrag", "compress=zlib", "compress=lzo", "compress=zstd", "compress=no", "compress-force=zlib", "compress-force=lzo", "compress-force=zstd", "discard", "noacl", "noatime", "nodatasum", "nospace_cache", "recovery", "skip_balance", "space_cache", "nossd", "ssd", "ssd_spread", "commit=120"};
#ifdef NDEVENV
            utils::exec("modprobe btrfs");
#endif
        } else if (file_sys == "ext2") {
            config_data["FILESYSTEM"] = "mkfs.ext2 -q";
        } else if (file_sys == "ext3") {
            config_data["FILESYSTEM"] = "mkfs.ext3 -q";
        } else if (file_sys == "ext4") {
            config_data["FILESYSTEM"] = "mkfs.ext4 -q";
            config_data["fs_opts"]    = std::vector<std::string>{"data=journal", "data=writeback", "dealloc", "discard", "noacl", "noatime", "nobarrier", "nodelalloc"};
        } else if (file_sys == "f2fs") {
            config_data["FILESYSTEM"] = "mkfs.f2fs -q";
            config_data["fs_opts"]    = std::vector<std::string>{"data_flush", "disable_roll_forward", "disable_ext_identify", "discard", "fastboot", "flush_merge", "inline_xattr", "inline_data", "inline_dentry", "no_heap", "noacl", "nobarrier", "noextent_cache", "noinline_data", "norecovery"};
#ifdef NDEVENV
            utils::exec("modprobe f2fs");
#endif
        } else if (file_sys == "jfs") {
            config_data["FILESYSTEM"] = "mkfs.jfs -q";
            config_data["fs_opts"]    = std::vector<std::string>{"discard", "errors=continue", "errors=panic", "nointegrity"};
        } else if (file_sys == "nilfs2") {
            config_data["FILESYSTEM"] = "mkfs.nilfs2 -fq";
            config_data["fs_opts"]    = std::vector<std::string>{"discard", "nobarrier", "errors=continue", "errors=panic", "order=relaxed", "order=strict", "norecovery"};
        } else if (file_sys == "ntfs") {
            config_data["FILESYSTEM"] = "mkfs.ntfs -q";
        } else if (file_sys == "reiserfs") {
            config_data["FILESYSTEM"] = "mkfs.reiserfs -q";
            config_data["fs_opts"]    = std::vector<std::string>{"acl", "nolog", "notail", "replayonly", "user_xattr"};
        } else if (file_sys == "vfat") {
            config_data["FILESYSTEM"] = "mkfs.vfat -F32";
        } else if (file_sys == "xfs") {
            config_data["FILESYSTEM"] = "mkfs.xfs -f";
            config_data["fs_opts"]    = std::vector<std::string>{"discard", "filestreams", "ikeep", "largeio", "noalign", "nobarrier", "norecovery", "noquota", "wsync"};
        }
        success = true;
        screen.ExitLoopClosure()();
    };
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen);

    /* clang-format off */
    if (!success) { return false; }
    /* clang-format on */

    // Warn about formatting!
    const auto& file_sys  = std::get<std::string>(config_data["FILESYSTEM"]);
    const auto& partition = std::get<std::string>(config_data["PARTITION"]);
    const auto& content   = fmt::format(FMT_COMPILE("\nMount {}\n \n! Data on {} will be lost !\n"), file_sys, partition);
    const auto& do_mount  = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    if (do_mount) {
#ifdef NDEVENV
        utils::exec(fmt::format(FMT_COMPILE("{} {}"), file_sys, partition));
#endif
        spdlog::info("mount.{} {}", partition, file_sys);
    }

    return success;
}

// This subfunction allows for special mounting options to be applied for relevant fs's.
// Separate subfunction for neatness.
void mount_opts() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& file_sys      = std::get<std::string>(config_data["FILESYSTEM"]);
    const auto& fs_opts       = std::get<std::vector<std::string>>(config_data["fs_opts"]);
    const auto& partition     = std::get<std::string>(config_data["PARTITION"]);
    const auto& format_name   = utils::exec(fmt::format(FMT_COMPILE("echo {} | rev | cut -d/ -f1 | rev"), partition));
    const auto& format_device = utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/disk/p\" | {}"), format_name, "awk \'/disk/ {print $1}\'"));

    const auto& rotational_queue = (utils::exec(fmt::format(FMT_COMPILE("cat /sys/block/{}/queue/rotational"), format_device)) == "1");

    std::unique_ptr<bool[]> fs_opts_state{new bool[fs_opts.size()]{false}};
    for (size_t i = 0; i < fs_opts.size(); ++i) {
        const auto& fs_opt = fs_opts[i];
        auto& fs_opt_state = fs_opts_state[i];
        if (rotational_queue) {
            fs_opt_state = ((fs_opt == "autodefrag")
                || (fs_opt == "compress=zlip")
                || (fs_opt == "nossd"));
        } else {
            fs_opt_state = ((fs_opt == "compress=lzo")
                || (fs_opt == "space_cache")
                || (fs_opt == "commit=120")
                || (fs_opt == "ssd"));
        }

        /* clang-format off */
        if (!fs_opt_state) { fs_opt_state = (fs_opt == "noatime"); }
        /* clang-format on */
    }

    auto screen           = ScreenInteractive::Fullscreen();
    auto& mount_opts_info = std::get<std::string>(config_data["MOUNT_OPTS"]);
    auto ok_callback      = [&] {
        mount_opts_info = detail::from_checklist_string(fs_opts, fs_opts_state.get());
        screen.ExitLoopClosure()();
    };

    const auto& file_sys_formatted = utils::exec(fmt::format(FMT_COMPILE("echo {} | sed \"s/.*\\.//g;s/-.*//g\""), file_sys));
    const auto& fs_title           = fmt::format(FMT_COMPILE("New CLI Installer | {}"), file_sys_formatted);
    const auto& content_size       = size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40) | vscroll_indicator | yframe | flex;

    static constexpr auto mount_options_body = "\nUse [Space] to de/select the desired mount\noptions and review carefully. Please do not\nselect multiple versions of the same option.\n";
    detail::checklist_widget(fs_opts, ok_callback, fs_opts_state.get(), &screen, mount_options_body, fs_title, {content_size, nothing});

    // Now clean up the file
    mount_opts_info = utils::exec(fmt::format(FMT_COMPILE("echo \"{}\" | sed \'s/ /,/g\'"), mount_opts_info));
    mount_opts_info = utils::exec(fmt::format(FMT_COMPILE("echo \"{}\" | sed \'$s/,$//\'"), mount_opts_info));

    // If mount options selected, confirm choice
    if (!mount_opts_info.empty()) {
        auto confirm_text    = Container::Vertical({
            Renderer([] { return paragraphAlignLeft("Confirm the following mount options:"); }),
            Renderer([&] { return text(mount_opts_info) | dim; }),
        });
        const auto& do_mount = detail::yesno_widget(confirm_text, size(HEIGHT, LESS_THAN, 10) | size(WIDTH, LESS_THAN, 75));
        /* clang-format off */
        if (!do_mount) { mount_opts_info = ""; }
        /* clang-format on */
    }
}

bool mount_current_partition() noexcept {
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& partition  = std::get<std::string>(config_data["PARTITION"]);
    const auto& mount_dev  = std::get<std::string>(config_data["MOUNT"]);

#ifdef NDEVENV
    std::error_code err{};
    // Make the mount directory
    fs::path mount_dir(fmt::format(FMT_COMPILE("{}{}"), mountpoint, mount_dev));
    fs::create_directories(mount_dir, err);
#endif

    config_data["MOUNT_OPTS"] = "";
    /* clang-format off */
    // Get mounting options for appropriate filesystems
    const auto& fs_opts = std::get<std::vector<std::string>>(config_data["fs_opts"]);
    if (!fs_opts.empty()) { mount_opts(); }
    /* clang-format on */

    // TODO: use libmount instead.
    // see https://github.com/util-linux/util-linux/blob/master/sys-utils/mount.c#L734
#ifdef NDEVENV
    const auto& mount_opts_info = std::get<std::string>(config_data["MOUNT_OPTS"]);
    if (!mount_opts_info.empty()) {
        // check_for_error "mount ${PARTITION} $(cat ${MOUNT_OPTS})"
        const auto& mount_status = utils::exec(fmt::format(FMT_COMPILE("mount -o {} {} {}{}"), mount_opts_info, partition, mountpoint, mount_dev));
        spdlog::info("{}", mount_status);
    } else {
        // check_for_error "mount ${PARTITION}"
        const auto& mount_status = utils::exec(fmt::format(FMT_COMPILE("mount {} {}{}"), partition, mountpoint, mount_dev));
        spdlog::info("{}", mount_status);
    }
#endif
    confirm_mount(fmt::format(FMT_COMPILE("{}{}"), mountpoint, mount_dev));

    // Identify if mounted partition is type "crypt" (LUKS on LVM, or LUKS alone)
    if (!utils::exec(fmt::format(FMT_COMPILE("lsblk -lno TYPE {} | grep \"crypt\""), partition)).empty()) {
        // cryptname for bootloader configuration either way
        config_data["LUKS"]          = 1;
        auto& luks_name              = std::get<std::string>(config_data["LUKS_NAME"]);
        const auto& luks_dev         = std::get<std::string>(config_data["LUKS_DEV"]);
        luks_name                    = utils::exec(fmt::format(FMT_COMPILE("echo {} | sed \"s~^/dev/mapper/~~g\""), partition));
        const auto& check_cryptparts = [&](const auto cryptparts, auto functor) {
            for (const auto& cryptpart : cryptparts) {
                if (!utils::exec(fmt::format(FMT_COMPILE("lsblk -lno NAME {} | grep \"{}\""), cryptpart, luks_name)).empty()) {
                    functor(cryptpart);
                    return true;
                }
            }
            return false;
        };

        // Check if LUKS on LVM (parent = lvm /dev/mapper/...)
        auto cryptparts    = utils::make_multiline(utils::exec("lsblk -lno NAME,FSTYPE,TYPE | grep \"lvm\" | grep -i \"crypto_luks\" | uniq | awk '{print \"/dev/mapper/\"$1}'"));
        auto check_functor = [&](const auto cryptpart) {
            config_data["LUKS_DEV"] = fmt::format(FMT_COMPILE("{} cryptdevice={}:{}"), luks_dev, cryptpart, luks_name);
            config_data["LVM"]      = 1;
        };
        if (check_cryptparts(cryptparts, check_functor)) {
            return true;
        }

        // Check if LVM on LUKS
        cryptparts = utils::make_multiline(utils::exec("lsblk -lno NAME,FSTYPE,TYPE | grep \" crypt$\" | grep -i \"LVM2_member\" | uniq | awk '{print \"/dev/mapper/\"$1}'"));
        if (check_cryptparts(cryptparts, check_functor)) {
            return true;
        }

        // Check if LUKS alone (parent = part /dev/...)
        cryptparts                 = utils::make_multiline(utils::exec("lsblk -lno NAME,FSTYPE,TYPE | grep \"part\" | grep -i \"crypto_luks\" | uniq | awk '{print \"/dev/\"$1}'"));
        const auto& check_func_dev = [&](const auto cryptpart) {
            auto& luks_uuid         = std::get<std::string>(config_data["LUKS_UUID"]);
            luks_uuid               = utils::exec(fmt::format(FMT_COMPILE("lsblk -lno UUID,TYPE,FSTYPE {} | grep \"part\" | grep -i \"crypto_luks\" | {}"), cryptpart, "awk '{print $1}'"));
            config_data["LUKS_DEV"] = fmt::format(FMT_COMPILE("{} cryptdevice=UUID={}:{}"), luks_dev, luks_uuid, luks_name);
        };
        if (check_cryptparts(cryptparts, check_func_dev)) {
            return true;
        }
    }
    /*

        // If LVM logical volume....
    elif [[ $(lsblk -lno TYPE ${PARTITION} | grep "lvm") != "" ]]; then
        LVM=1

        // First get crypt name (code above would get lv name)
        cryptparts=$(lsblk -lno NAME,TYPE,FSTYPE | grep "crypt" | grep -i "lvm2_member" | uniq | awk '{print "/dev/mapper/"$1}')
        for i in ${cryptparts}; do
            if [[ $(lsblk -lno NAME ${i} | grep $(echo $PARTITION | sed "s~^/dev/mapper/~~g")) != "" ]]; then
                LUKS_NAME=$(echo ${i} | sed s~/dev/mapper/~~g)
                return 0;
            fi
        done

        // Now get the device (/dev/...) for the crypt name
        cryptparts=$(lsblk -lno NAME,FSTYPE,TYPE | grep "part" | grep -i "crypto_luks" | uniq | awk '{print "/dev/"$1}')
        for i in ${cryptparts}; do
            if [[ $(lsblk -lno NAME ${i} | grep $LUKS_NAME) != "" ]]; then
                # Create UUID for comparison
                LUKS_UUID=$(lsblk -lno UUID,TYPE,FSTYPE ${i} | grep "part" | grep -i "crypto_luks" | awk '{print $1}')

                # Check if not already added as a LUKS DEVICE (i.e. multiple LVs on one crypt). If not, add.
                if [[ $(echo $LUKS_DEV | grep $LUKS_UUID) == "" ]]; then
                    LUKS_DEV="$LUKS_DEV cryptdevice=UUID=$LUKS_UUID:$LUKS_NAME"
                    LUKS=1
                fi

                return 0;
            fi
    fi*/
    return true;
}

void make_swap() noexcept {
    static constexpr auto SelSwpNone = "None";
    static constexpr auto SelSwpFile = "Swapfile";

    auto* config_instance       = Config::instance();
    auto& config_data           = config_instance->data();
    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);

    std::string answer{};
    {
        std::vector<std::string> temp{"None -"};
        const auto& root_filesystem = utils::exec(fmt::format(FMT_COMPILE("findmnt -ln -o FSTYPE \"{}\""), mountpoint_info));
        if (!(root_filesystem == "zfs" || root_filesystem == "btrfs")) {
            temp.push_back("Swapfile -");
        }
        const auto& partitions = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);
        temp.reserve(partitions.size());
        ranges::copy(partitions, std::back_inserter(temp));

        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        bool success{};
        auto ok_callback = [&] {
            const auto& src   = temp[static_cast<std::size_t>(selected)];
            const auto& lines = utils::make_multiline(src, false, " ");
            answer            = lines[0];
            success           = true;
            screen.ExitLoopClosure()();
        };
        /* clang-format off */
        detail::menu_widget(temp, ok_callback, &selected, &screen);
        if (!success) { return; }
        /* clang-format on */
    }

    auto& partition = std::get<std::string>(config_data["PARTITION"]);
    /* clang-format off */
    if (answer == SelSwpNone) { return; }
    partition = answer;
    /* clang-format on */

    if (partition == SelSwpFile) {
        const auto& total_memory = utils::exec("grep MemTotal /proc/meminfo | awk \'{print $2/1024}\' | sed \'s/\\..*//\'");
        std::string value{fmt::format(FMT_COMPILE("{}M"), total_memory)};
        if (!detail::inputbox_widget(value, "\nM = MB, G = GB\n", size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
            return;
        }

        while (utils::exec(fmt::format(FMT_COMPILE("echo \"{}\" | grep \"M\\|G\""), value)) == "") {
            detail::msgbox_widget(fmt::format(FMT_COMPILE("\n{} Error: M = MB, G = GB\n"), SelSwpFile));
            value = fmt::format(FMT_COMPILE("{}M"), total_memory);
            if (!detail::inputbox_widget(value, "\nM = MB, G = GB\n", size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
                return;
            }
        }

#ifdef NDEVENV
        const auto& swapfile_path = fmt::format(FMT_COMPILE("{}/swapfile"), mountpoint_info);
        utils::exec(fmt::format(FMT_COMPILE("fallocate -l {} {} &>/dev/null"), value, swapfile_path));
        utils::exec(fmt::format(FMT_COMPILE("chmod 600 {}"), swapfile_path));
        utils::exec(fmt::format(FMT_COMPILE("mkswap {} &>/dev/null"), swapfile_path));
        utils::exec(fmt::format(FMT_COMPILE("swapon {} &>/dev/null"), swapfile_path));
#endif
        return;
    }

    auto& partitions        = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);
    auto& number_partitions = std::get<std::int32_t>(config_data["NUMBER_PARTITIONS"]);

    // Warn user if creating a new swap
    const auto& swap_part = utils::exec(fmt::format(FMT_COMPILE("lsblk -o FSTYPE \"{}\" | grep -i \"swap\""), partition));
    if (swap_part != "swap") {
        const auto& do_swap = detail::yesno_widget(fmt::format(FMT_COMPILE("\nmkswap {}\n"), partition), size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
        /* clang-format off */
        if (!do_swap) { return; }
        /* clang-format on */

#ifdef NDEVENV
        utils::exec(fmt::format(FMT_COMPILE("mkswap {} &>/dev/null"), partition));
#endif
        spdlog::info("mkswap.{}", partition);
    }

#ifdef NDEVENV
    // Whether existing to newly created, activate swap
    utils::exec(fmt::format(FMT_COMPILE("swapon {} &>/dev/null"), partition));
#endif

    // TODO: reimplement natively
    // Since a partition was used, remove that partition from the list
    const auto& str      = utils::make_multiline(partitions);
    const auto& cmd      = fmt::format(FMT_COMPILE("echo \"{0}\" | sed \"s~{1} [0-9]*[G-M]~~\" | sed \"s~{1} [0-9]*\\.[0-9]*[G-M]~~\" | sed \"s~{1}$\' -\'~~\""), str, partition);
    const auto& res_text = utils::exec(cmd);
    partitions           = utils::make_multiline(res_text);
    number_partitions -= 1;
}

void lvm_detect() noexcept {
    const auto& lvm_pv = utils::exec("pvs -o pv_name --noheading 2>/dev/null");
    const auto& lvm_vg = utils::exec("vgs -o vg_name --noheading 2>/dev/null");
    const auto& lvm_lv = utils::exec("lvs -o vg_name,lv_name --noheading --separator - 2>/dev/null");

    if ((lvm_lv != "") && (lvm_vg != "") && (lvm_pv != "")) {
        detail::infobox_widget("\nExisting Logical Volume Management (LVM) detected.\nActivating. Please Wait...\n");
        std::this_thread::sleep_for(std::chrono::seconds(2));
#ifdef NDEVENV
        utils::exec("modprobe dm-mod");
        utils::exec("vgscan >/dev/null 2>&1");
        utils::exec("vgchange -ay >/dev/null 2>&1");
#endif
    }
}

void lvm_del_vg() noexcept {
    // Generate list of VGs for selection
    const auto& vg_list = utils::lvm_show_vg();

    // If no VGs, no point in continuing
    if (vg_list.empty()) {
        detail::msgbox_widget("\nNo Volume Groups found.\n");
        return;
    }

    // Select VG
    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    std::string sel_vg{};
    auto ok_callback = [&] {
        sel_vg = vg_list[static_cast<size_t>(selected)];
        screen.ExitLoopClosure()();
    };
    /* clang-format off */
    static constexpr auto del_lvmvg_body = "\nSelect Volume Group to delete.\nAll Logical Volumes within will also be deleted.\n";
    detail::menu_widget(vg_list, ok_callback, &selected, &screen, del_lvmvg_body);
    if (sel_vg.empty()) { return; }
    /* clang-format on */

    // Ask for confirmation
    const auto& do_action = detail::yesno_widget("\nConfirm deletion of Volume Group(s) and Logical Volume(s).\n");
    /* clang-format off */
    if (!do_action) { return; }
    /* clang-format on */

#ifdef NDEVENV
    // if confirmation given, delete
    utils::exec(fmt::format(FMT_COMPILE("vgremove -f {} 2>/dev/null"), sel_vg), true);
#endif
}

void lvm_menu() noexcept {
    const std::vector<std::string> menu_entries = {
        "Create VG and LV(s)",
        "Delete Volume Groups",
        "Delete *ALL* VGs, LVs, PVs",
        "Back",
    };

    tui::lvm_detect();

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 1:
            tui::lvm_del_vg();
            break;
        case 0:
        case 2:
            break;
        default:
            screen.ExitLoopClosure()();
            return;
        }
    };

    static constexpr auto lvm_menu_body = "\nLogical Volume Management (LVM) allows 'virtual' hard drives (Volume Groups)\nand partitions (Logical Volumes) to be created from existing drives\nand partitions. A Volume Group must be created first, then one or more\nLogical Volumes in it.\n \nLVM can also be used with an encrypted partition to create multiple logical\nvolumes (e.g. root and home) in it.\n";
    const auto& content_size            = size(HEIGHT, LESS_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, lvm_menu_body, {size(HEIGHT, LESS_THAN, 18), content_size});
}

void make_esp() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    const auto& sys_info  = std::get<std::string>(config_data["SYSTEM"]);

    /* clang-format off */
    if (sys_info != "UEFI") { return; }
    /* clang-format on */

    std::string answer{};
    {
        const auto& partitions = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);

        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        bool success{};
        auto ok_callback = [&] {
            const auto& src   = partitions[static_cast<std::size_t>(selected)];
            const auto& lines = utils::make_multiline(src, false, " ");
            answer            = lines[0];
            success           = true;
            screen.ExitLoopClosure()();
        };
        /* clang-format off */
        detail::menu_widget(partitions, ok_callback, &selected, &screen);
        if (!success) { return; }
        /* clang-format on */
    }

    const auto& luks          = std::get<std::int32_t>(config_data["LUKS"]);
    auto& partition           = std::get<std::string>(config_data["PARTITION"]);
    partition                 = answer;
    config_data["UEFI_MOUNT"] = "";
    config_data["UEFI_PART"]  = partition;

    // If it is already a fat/vfat partition...
    const auto& ret_status = utils::exec(fmt::format(FMT_COMPILE("fsck -N {} | grep fat"), partition), true);
    bool do_boot_partition{};
    if (ret_status != "0") {
        const auto& content = fmt::format(FMT_COMPILE("\nThe UEFI partition {} has already been formatted.\n \nReformat? Doing so will erase ALL data already on that partition.\n"), partition);
        do_boot_partition   = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    }
    if (do_boot_partition) {
#ifdef NDEVENV
        utils::exec(fmt::format(FMT_COMPILE("mkfs.vfat -F32 {} &>/dev/null"), partition));
#endif
        spdlog::debug("Formating boot partition with fat/vfat!");
    }

    {
        static constexpr auto MntUefiBody            = "\nSelect UEFI Mountpoint.\n \n/boot/efi is recommended for multiboot systems.\n/boot is required for systemd-boot.\n";
        static constexpr auto MntUefiCrypt           = "\nSelect UEFI Mountpoint.\n \n/boot/efi is recommended for multiboot systems and required for full disk encryption.\nEncrypted /boot is supported only by grub and can lead to slow startup.\n \n/boot is required for systemd-boot and for refind when using encryption.\n";
        const auto& MntUefiMessage                   = (luks == 0) ? MntUefiBody : MntUefiCrypt;
        const std::vector<std::string> radiobox_list = {
            "/boot/efi",
            "/boot",
        };
        answer.clear();

        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        auto ok_callback = [&] {
            answer = radiobox_list[static_cast<std::size_t>(selected)];
            screen.ExitLoopClosure()();
        };
        detail::radiolist_widget(radiobox_list, ok_callback, &selected, &screen, MntUefiMessage, {.text_size = nothing});
    }

    /* clang-format off */
    if (answer.empty()) { return; }
    /* clang-format on */

    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    auto& uefi_mount            = std::get<std::string>(config_data["UEFI_MOUNT"]);
    uefi_mount                  = answer;
    const auto& path_formatted  = fmt::format(FMT_COMPILE("{}{}"), mountpoint_info, uefi_mount);
#ifdef NDEVENV
    utils::exec(fmt::format(FMT_COMPILE("mkdir -p {}"), path_formatted));
    utils::exec(fmt::format(FMT_COMPILE("mount {} {}"), partition, path_formatted));
#endif
    confirm_mount(path_formatted);
}

void mount_partitions() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    // Warn users that they CAN mount partitions without formatting them!
    static constexpr auto content = "\nIMPORTANT: Partitions can be mounted without formatting them\nby selecting the 'Do not format' option listed at the top of\nthe file system menu.\n \nEnsure the correct choices for mounting and formatting\nare made as no warnings will be provided, with the exception of\nthe UEFI boot partition.\n";
    detail::msgbox_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 70));

    // LVM Detection. If detected, activate.
    tui::lvm_detect();

    // Ensure partitions are unmounted (i.e. where mounted previously)
    config_data["INCLUDE_PART"] = "part\\|lvm\\|crypt";
    utils::umount_partitions();

    // We need to remount the zfs filesystems that have defined mountpoints already
    // zfs mount -aO 2>/dev/null

    // Get list of available partitions
    utils::find_partitions();

    // Add legacy zfs filesystems to the list - these can be mounted but not formatted
    /*for i in $(zfs_list_datasets "legacy"); do
        PARTITIONS="${PARTITIONS} ${i}"
        PARTITIONS="${PARTITIONS} zfs"
        NUMBER_PARTITIONS=$(( NUMBER_PARTITIONS + 1 ))
    done*/

    // Filter out partitions that have already been mounted and partitions that just contain crypt or zfs devices
    auto ignore_part = utils::list_mounted();
    // ignore_part += utils::zfs_list_devs();
    ignore_part += utils::list_containing_crypt();

    /* const auto& parts = utils::make_multiline(ignore_part);
    for (const auto& part : parts) {
        utils::delete_partition_in_list(part);
    }*/

    // check to see if we already have a zfs root mounted
    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    if (utils::exec(fmt::format(FMT_COMPILE("findmnt -ln -o FSTYPE \"{}\""), mountpoint_info)) == "zfs") {
        detail::infobox_widget("\nUsing ZFS root on \'/\'\n");
        std::this_thread::sleep_for(std::chrono::seconds(3));
    } else {
        // Identify and mount root
        {
            auto screen = ScreenInteractive::Fullscreen();
            std::int32_t selected{};
            bool success{};
            const auto& partitions = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);

            auto ok_callback = [&] {
                const auto& src          = partitions[static_cast<std::size_t>(selected)];
                const auto& lines        = utils::make_multiline(src, false, " ");
                config_data["PARTITION"] = lines[0];
                config_data["ROOT_PART"] = lines[0];
                success                  = true;
                screen.ExitLoopClosure()();
            };
            /* clang-format off */
            detail::menu_widget(partitions, ok_callback, &selected, &screen);
            if (!success) { return; }
            /* clang-format on */
        }
        const auto& root_part = std::get<std::string>(config_data["ROOT_PART"]);
        const auto& part      = std::get<std::string>(config_data["PARTITION"]);
        spdlog::info("partition: {}", part);

        // Reset the mountpoint variable, in case this is the second time through this menu and old state is still around
        config_data["MOUNT"] = "";

        // Format with FS (or skip) -> // Make the directory and mount. Also identify LUKS and/or LVM
        /* clang-format off */
        if (!tui::select_filesystem()) { return; }
        if (!tui::mount_current_partition()) { return; }
        /* clang-format on */

        // utils::delete_partition_in_list(std::get<std::string>(config_data["ROOT_PART"]));

        // Extra check if root is on LUKS or lvm
        // get_cryptroot
        // echo "$LUKS_DEV" > /tmp/.luks_dev
        // If the root partition is btrfs, offer to create subvolumus
        if (utils::exec(fmt::format(FMT_COMPILE("findmnt -no FSTYPE \"{}\""), mountpoint_info)) == "btrfs") {
            // Check if there are subvolumes already on the btrfs partition
            const auto& subvolumes       = fmt::format(FMT_COMPILE("btrfs subvolume list \"{}\""), mountpoint_info);
            const auto& subvolumes_count = utils::exec(fmt::format(FMT_COMPILE("{} | wc -l"), subvolumes));
            const auto& lines_count      = utils::to_int(subvolumes_count.data());
            if (lines_count > 1) {
                const auto& subvolumes_formated = utils::exec(fmt::format(FMT_COMPILE("{} | cut -d\" \" -f9"), subvolumes));
                const auto& existing_subvolumes = detail::yesno_widget(fmt::format(FMT_COMPILE("\nFound subvolumes {}\n \nWould you like to mount them?\n "), subvolumes_formated), size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
                // Pre-existing subvolumes and user wants to mount them
                /* clang-format off */
                if (existing_subvolumes) { utils::mount_existing_subvols({root_part, part}); }
                /* clang-format on */
            } else {
                // No subvolumes present. Make some new ones
                const auto& create_subvolumes = detail::yesno_widget("\nWould you like to create subvolumes in it? \n", size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
                /* clang-format on */
                if (create_subvolumes) {
                    tui::btrfs_subvolumes();
                }
                /* clang-format on */
            }
        }
    }

    // We need to remove legacy zfs partitions before make_swap since they can't hold swap
    // local zlegacy
    // for zlegacy in $(zfs_list_datasets "legacy"); do
    //    delete_partition_in_list ${zlegacy}
    // done

    // Identify and create swap, if applicable
    tui::make_swap();

    // Now that swap is done we put the legacy partitions back, unless they are already mounted
    /*for i in $(zfs_list_datasets "legacy"); do
        PARTITIONS="${PARTITIONS} ${i}"
        PARTITIONS="${PARTITIONS} zfs"
        NUMBER_PARTITIONS=$(( NUMBER_PARTITIONS + 1 ))
    done*/

    /*const auto& parts_tmp = utils::make_multiline(utils::list_mounted());
    for (const auto& part : parts_tmp) {
        utils::delete_partition_in_list(part);
    }*/

    // All other partitions
    const auto& number_partitions = std::get<std::int32_t>(config_data["NUMBER_PARTITIONS"]);
    const auto& system_info       = std::get<std::string>(config_data["SYSTEM"]);
    const auto& partition         = std::get<std::string>(config_data["PARTITION"]);
    while (number_partitions > 0) {
        {
            const auto& partitions = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);
            std::vector<std::string> temp{"Done -"};
            temp.reserve(partitions.size());
            ranges::copy(partitions, std::back_inserter(temp));

            auto screen = ScreenInteractive::Fullscreen();
            std::int32_t selected{};
            bool success{};
            auto ok_callback = [&] {
                const auto& src          = temp[static_cast<std::size_t>(selected)];
                const auto& lines        = utils::make_multiline(src, false, " ");
                config_data["PARTITION"] = lines[0];
                success                  = true;
                screen.ExitLoopClosure()();
            };
            /* clang-format off */
            detail::menu_widget(temp, ok_callback, &selected, &screen);
            if (!success) { return; }
            /* clang-format on */
        }

        if (partition == "Done") {
            make_esp();
            utils::get_cryptroot();
            utils::get_cryptboot();
            return;
        }
        config_data["MOUNT"] = "";
        tui::select_filesystem();

        /* clang-format off */
        // Ask user for mountpoint. Don't give /boot as an example for UEFI systems!
        std::string_view mnt_examples = "/boot\n/home\n/var";
        if (system_info == "UEFI") { mnt_examples = "/home\n/var"; }

        std::string value{"/"};
        static constexpr auto ExtPartBody1 = "Specify partition mountpoint. Ensure\nthe name begins with a forward slash (/).\nExamples include:";
        if (!detail::inputbox_widget(value, fmt::format(FMT_COMPILE("\n{}\n{}\n"), ExtPartBody1, mnt_examples))) { return; }
        /* clang-format on */
        auto& mount_dev = std::get<std::string>(config_data["MOUNT"]);
        mount_dev       = std::move(value);

        // loop while the mountpoint specified is incorrect (is only '/', is blank, or has spaces).
        while ((mount_dev.size() <= 1) || (mount_dev[0] != '/') || (mount_dev.find_first_of(" ") != std::string::npos)) {
            // Warn user about naming convention
            detail::msgbox_widget("\nPartition cannot be mounted due to a problem with the mountpoint name.\nA name must be given after a forward slash.\n");
            // Ask user for mountpoint again
            value = "/";
            if (!detail::inputbox_widget(value, fmt::format(FMT_COMPILE("\n{}\n{}\n"), ExtPartBody1, mnt_examples))) {
                return;
            }
            mount_dev = std::move(value);
        }
        // Create directory and mount.
        tui::mount_current_partition();
        // utils::delete_partition_in_list(partition);

        // Determine if a separate /boot is used.
        // 0 = no separate boot,
        // 1 = separate non-lvm boot,
        // 2 = separate lvm boot. For Grub configuration
        if (mount_dev == "/boot") {
            const auto& cmd             = fmt::format(FMT_COMPILE("lsblk -lno TYPE {} | grep \"lvm\""), partition);
            const auto& cmd_out         = utils::exec(cmd);
            config_data["LVM_SEP_BOOT"] = 1;
            if (!cmd_out.empty()) {
                config_data["LVM_SEP_BOOT"] = 2;
            }
        }
    }
}

void configure_mirrorlist() noexcept {
    const std::vector<std::string> menu_entries = {
        "Edit Pacman Configuration",
        "Rank mirrors by speed",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            screen.Suspend();
            tui::edit_pacman_conf();
            screen.Resume();
            break;
        case 1:
            SPDLOG_ERROR("Implement me!");
            break;
        default:
            screen.ExitLoopClosure()();
            break;
        }
    };
    static constexpr auto mirrorlist_body = "\nThe pacman configuration file can be edited\nto enable multilib and other repositories.\n";
    const auto& content_size              = size(HEIGHT, LESS_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, mirrorlist_body, {size(HEIGHT, LESS_THAN, 3), content_size});
}

void create_partitions() noexcept {
    static constexpr std::string_view optwipe = "Securely Wipe Device (optional)";
    static constexpr std::string_view optauto = "Automatic Partitioning";

    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const std::vector<std::string> menu_entries = {
        optwipe.data(),
        optauto.data(),
        "cfdisk",
        "cgdisk",
        "fdisk",
        "gdisk",
        "parted",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        const auto& selected_entry = menu_entries[static_cast<std::size_t>(selected)];
        if (selected_entry != optwipe && selected_entry != optauto) {
            screen.Suspend();
#ifdef NDEVENV
            utils::exec(fmt::format(FMT_COMPILE("{} {}"), selected_entry, std::get<std::string>(config_data["DEVICE"])), true);
#else
            spdlog::debug("to be executed: {}", fmt::format(FMT_COMPILE("{} {}"), selected_entry, std::get<std::string>(config_data["DEVICE"])));
#endif
            screen.Resume();
            screen.ExitLoopClosure()();
            return;
        }

        if (selected_entry == optwipe) {
            utils::secure_wipe();
        }
        if (selected_entry == optauto) {
            tui::auto_partition();
        }
        screen.ExitLoopClosure()();
    };
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen);
}

void install_core_menu() noexcept {
    const std::vector<std::string> menu_entries = {
        "Install Base Packages",
        "Install Desktop Environment",
        "Install Bootloader",
        "Configure Base",
        "Install Custom Packages",
        "System Tweaks",
        "Review Configuration Files",
        "Chroot into Installation",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            if (!utils::check_mount()) {
                screen.ExitLoopClosure()();
                break;
            }
            tui::install_base();
            break;
        case 1:
            if (!utils::check_mount()) {
                screen.ExitLoopClosure()();
                break;
            }
            tui::install_desktop();
            break;
        case 2: {
            if (!utils::check_base()) {
                screen.ExitLoopClosure()();
                break;
            }
            tui::install_bootloader();
            break;
        }
        case 3: {
            if (!utils::check_base()) {
                screen.ExitLoopClosure()();
                break;
            }
            tui::config_base_menu();
            break;
        }
        case 4:
            tui::install_cust_pkgs();
            break;
        case 5:
            if (!utils::check_base()) {
                screen.ExitLoopClosure()();
            }
            tui::tweaks_menu();
            break;
        case 6:
            if (!utils::check_base()) {
                screen.ExitLoopClosure()();
            }
            tui::edit_configs();
            break;
        case 7:
            if (!utils::check_base()) {
                screen.ExitLoopClosure()();
            }
            screen.Suspend();
            tui::chroot_interactive();
            screen.Resume();

            break;
        default:
            screen.ExitLoopClosure()();
            break;
        }
    };
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen);
}

void system_rescue_menu() noexcept {
    const std::vector<std::string> menu_entries = {
        "Install Hardware Drivers",
        "Install Bootloader",
        "Install Packages",
        "Remove Packages",
        "Review Configuration Files",
        "Chroot into Installation",
        "Data Recovery",
        "View System Logs",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            if (!utils::check_mount() && !utils::check_base()) {
                screen.ExitLoopClosure()();
                break;
            }
            tui::install_drivers_menu();
            break;
        case 1:
            if (!utils::check_mount() && !utils::check_base()) {
                screen.ExitLoopClosure()();
                break;
            }
            tui::install_bootloader();
            break;
        case 2:
            if (!utils::check_mount()) {
                screen.ExitLoopClosure()();
                break;
            }
            tui::install_cust_pkgs();
            break;
        case 3:
            if (!utils::check_base()) {
                screen.ExitLoopClosure()();
                break;
            }
            tui::rm_pgs();
            break;
        case 4:
            if (!utils::check_base()) {
                screen.ExitLoopClosure()();
            }
            tui::edit_configs();
            break;
        case 5:
            if (!utils::check_base()) {
                screen.ExitLoopClosure()();
            }
            screen.Suspend();
            tui::chroot_interactive();
            screen.Resume();

            break;
        case 7:
            if (!utils::check_base()) {
                screen.ExitLoopClosure()();
            }
            tui::logs_menu();
            break;
        default:
            screen.ExitLoopClosure()();
            break;
        }
    };
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen);
}

void prep_menu() noexcept {
    const std::vector<std::string> menu_entries = {
        "Set Virtual Console",
        "List Devices (optional)",
        "Partition Disk",
        "RAID (optional)",
        "Logical Volume Management (optional)",
        "LUKS Encryption (optional)",
        "ZFS (optional)",
        "Mount Partitions",
        "Configure Installer Mirrorlist",
        "Refresh Pacman Keys",
        "Choose pacman cache",
        "Enable fsck hook",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            tui::select_keymap();
            utils::set_keymap();
            break;
        case 1:
            tui::show_devices();
            break;
        case 2: {
            utils::umount_partitions();
            if (tui::select_device()) {
                tui::create_partitions();
            }
            break;
        }
        case 4:
            tui::lvm_menu();
            break;
        case 5:
            tui::luks_menu_advanced();
            break;
        case 7:
            tui::mount_partitions();
            break;
        case 3:
        case 6:
            SPDLOG_ERROR("Implement me!");
            break;
        case 8:
            tui::configure_mirrorlist();
            break;
        case 9:
            tui::refresh_pacman_keys();
            break;
        case 10:
            tui::set_cache();
            break;
        case 11:
            tui::set_fsck_hook();
            break;
        default:
            screen.ExitLoopClosure()();
            break;
        }
    };
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, "", {size(HEIGHT, GREATER_THAN, 15) | size(WIDTH, GREATER_THAN, 50)});
}

void menu_advanced() noexcept {
    const std::vector<std::string> menu_entries = {
        "Prepare Installation",
        "Install System",
        "System Rescue",
        "Done",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            tui::prep_menu();
            break;
        case 1: {
            if (!utils::check_mount()) {
                screen.ExitLoopClosure()();
            }
            tui::install_core_menu();
            break;
        }
        case 2:
            tui::system_rescue_menu();
            break;
        default: {
            if (tui::exit_done()) {
                screen.ExitLoopClosure()();
            }
            break;
        }
        }
    };
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen);
}

void menu_simple() noexcept {
    // some simple ui
    const std::vector<std::string> menu_entries = {
        "entry 1",
        "entry 2",
        "entry 3",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    bool success{};
    auto ok_callback = [&] {
        success = true;
        screen.ExitLoopClosure()();
    };
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen);
    /* clang-format off */
    if (!success) { return; }
    /* clang-format on */

    fmt::print("Selected element = {}\n", selected);
}

void init() noexcept {
#if 0
    const std::vector<std::string> menu_entries = {
        "Simple installation",
        "Advanced installation",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    bool success{};
    auto ok_callback = [&] {
        success = true;
        screen.ExitLoopClosure()();
    };
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen);
    /* clang-format off */
    if (!success) { return; }
    /* clang-format on */

    switch (selected) {
    case 0:
        tui::menu_simple();
        break;
    case 1:
        tui::menu_advanced();
        break;
    default:
        break;
    }
#endif
    tui::menu_advanced();
}

}  // namespace tui
