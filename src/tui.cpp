#include "tui.hpp"
#include "crypto.hpp"
#include "definitions.hpp"
#include "disk.hpp"
#include "drivers.hpp"
#include "global_storage.hpp"
#include "misc.hpp"
#include "simple_tui.hpp"
#include "utils.hpp"
#include "widgets.hpp"

// import gucc
#include "gucc/btrfs.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/locale.hpp"
#include "gucc/mount_partitions.hpp"
#include "gucc/partition.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/swap.hpp"
#include "gucc/system_query.hpp"
#include "gucc/timezone.hpp"
#include "gucc/zfs.hpp"

#include <algorithm>    // for copy, transform
#include <cstdlib>      // for setenv
#include <filesystem>   // for exists, is_directory
#include <fstream>      // for ofstream
#include <ranges>       // for ranges::*
#include <string>       // for basic_string
#include <sys/mount.h>  // for mount

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

/* clang-format off */
#include <ctre.hpp>                                // for ctre::match
#include <ftxui/component/component.hpp>           // for Renderer, Button
#include <ftxui/component/screen_interactive.hpp>  // for Component, ScreenI...
#include <ftxui/dom/elements.hpp>                  // for operator|, size
/* clang-format on */

using namespace ftxui;
namespace fs = std::filesystem;

#include <ranges>

#ifdef NDEVENV
#include "follow_process_log.hpp"
#endif

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace tui {

bool exit_done() noexcept {
#ifdef NDEVENV
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    static constexpr auto close_inst_body = "\nClose installer?\n"sv;
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

        static constexpr auto LogInfo = "Would you like to save\nthe installation-log\nto the installed system?\nIt will be copied to\n"sv;
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

// Function will not allow incorrect UUID type for installed system.
void generate_fstab() noexcept {
    static constexpr auto fstab_body = "\nThe FSTAB file (File System TABle) sets what storage devices\nand partitions are to be mounted, and how they are to be used.\n\nUUID (Universally Unique IDentifier) used.\n"sv;
    const auto& do_set_fstab         = detail::yesno_widget(fstab_body, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    /* clang-format off */
    if (!do_set_fstab) { return; }
    /* clang-format on */

    utils::generate_fstab();
}

// Set system hostname
void set_hostname() noexcept {
    std::string hostname{"cachyos"};
    static constexpr auto hostname_body = "\nThe hostname is used to identify the system on a network.\n \nIt is restricted to alphanumeric characters, can contain a hyphen\n(-) - but not at the start or end - and must be no longer than 63 characters.\n"sv;
    if (!detail::inputbox_widget(hostname, hostname_body, size(HEIGHT, GREATER_THAN, 4))) {
        return;
    }
    // If at least one package, install.
    /* clang-format off */
    if (hostname.empty()) { return; }
    /* clang-format on */

    utils::set_hostname(hostname);
}

// Set system language
void set_locale() noexcept {
    const auto& locales = gucc::locale::get_possible_locales();

    std::int32_t selected{};
    for (std::size_t i = 0; i < locales.size(); ++i) {
        if (locales[i] == "en_US.UTF-8"sv) {
            selected = static_cast<std::int32_t>(i);
            break;
        }
    }

    // System language
    std::string locale{};
    {
        auto screen      = ScreenInteractive::Fullscreen();
        auto ok_callback = [&] {
            locale = locales[static_cast<std::size_t>(selected)];
            screen.ExitLoopClosure()();
        };

        static constexpr auto langBody = "\nChoose the system language.\n\nThe format is language_COUNTRY (e.g. en_US is english, United States;\nen_GB is english, Great Britain).\n"sv;
        auto content_size              = size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40) | vscroll_indicator | yframe | flex;
        detail::menu_widget(locales, ok_callback, &selected, &screen, langBody, {std::move(content_size), size(HEIGHT, GREATER_THAN, 1)});
    }
    /* clang-format off */
    if (locale.empty()) { return; }
    /* clang-format on */

    utils::set_locale(locale);
}

// Set keymap for X11
void set_xkbmap() noexcept {
    const auto& xkbmap_list = gucc::locale::get_x11_keymap_layouts();
    if (xkbmap_list.empty()) {
        detail::msgbox_widget("\nUnable to get X11 keymap layouts.\n"sv);
        return;
    }

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    for (std::size_t i = 0; i < xkbmap_list.size(); ++i) {
        if (xkbmap_list[i] == "us"sv) {
            selected = static_cast<std::int32_t>(i);
            break;
        }
    }
    bool success{};
    std::string xkbmap_choice{};
    auto ok_callback = [&] {
        xkbmap_choice = xkbmap_list[static_cast<std::size_t>(selected)];
        success       = true;
        screen.ExitLoopClosure()();
    };

    static constexpr auto xkbmap_body = "\nSelect Desktop Environment Keymap.\n"sv;
    auto content_size                 = size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40) | vscroll_indicator | yframe | flex;
    detail::menu_widget(xkbmap_list, ok_callback, &selected, &screen, xkbmap_body, {std::move(content_size), size(HEIGHT, GREATER_THAN, 1)});

    /* clang-format off */
    if (!success) { return; }
    /* clang-format on */

    utils::set_xkbmap(xkbmap_choice);
}

void select_keymap() noexcept {
    auto* config_instance      = Config::instance();
    auto& config_data          = config_instance->data();
    const auto& current_keymap = std::get<std::string>(config_data["KEYMAP"]);

    // does user want to change the default settings?
    static constexpr auto default_keymap_msg = "Currently configured keymap setting is:"sv;
    const auto& content                      = fmt::format(FMT_COMPILE("\n {}\n \n[ {} ]\n"), default_keymap_msg, current_keymap);
    const auto& keep_default                 = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    /* clang-format off */
    if (!keep_default) { return; }
    /* clang-format on */

    const auto& keymaps = gucc::utils::make_multiline(gucc::utils::exec(R"(ls -R /usr/share/kbd/keymaps | grep "map.gz" | sed 's/\.map\.gz//g' | sort)"));

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    for (std::size_t i = 0; i < keymaps.size(); ++i) {
        if (keymaps[i] == "us"sv) {
            selected = static_cast<std::int32_t>(i);
            break;
        }
    }
    bool success{};
    std::string keymap_choice{};
    auto ok_callback = [&] {
        keymap_choice = keymaps[static_cast<std::size_t>(selected)];
        success       = true;
        screen.ExitLoopClosure()();
    };
    static constexpr auto vc_keymap_body = "\nA virtual console is a shell prompt in a non-graphical environment.\nIts keymap is independent of a desktop environment / terminal.\n"sv;
    auto content_size                    = size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40) | vscroll_indicator | yframe | flex;
    detail::menu_widget(keymaps, ok_callback, &selected, &screen, vc_keymap_body, {std::move(content_size), size(HEIGHT, GREATER_THAN, 1)});

    /* clang-format off */
    if (!success) { return; }
    /* clang-format on */

    utils::set_keymap(keymap_choice);
}

// Set Zone and Sub-Zone
bool set_timezone() noexcept {
    std::string zone{};
    {
        auto screen           = ScreenInteractive::Fullscreen();
        const auto& zone_list = gucc::timezone::get_timezone_regions();

        std::int32_t selected{};
        auto ok_callback = [&] {
            zone = zone_list[static_cast<std::size_t>(selected)];
            screen.ExitLoopClosure()();
        };

        static constexpr auto timezone_body = "The time zone is used to correctly set your system clock."sv;
        auto content_size                   = size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
        detail::menu_widget(zone_list, ok_callback, &selected, &screen, timezone_body, {std::move(content_size), size(HEIGHT, GREATER_THAN, 1)});
    }
    /* clang-format off */
    if (zone.empty()) { return false; }
    /* clang-format on */

    std::string subzone{};
    {
        auto screen           = ScreenInteractive::Fullscreen();
        const auto& city_list = gucc::timezone::get_timezone_zones(zone);

        std::int32_t selected{};
        auto ok_callback = [&] {
            subzone = city_list[static_cast<std::size_t>(selected)];
            screen.ExitLoopClosure()();
        };

        static constexpr auto sub_timezone_body = "Select the city nearest to you."sv;
        auto content_size                       = size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40) | vscroll_indicator | yframe | flex;
        detail::menu_widget(city_list, ok_callback, &selected, &screen, sub_timezone_body, {std::move(content_size), size(HEIGHT, GREATER_THAN, 1)});
    }

    /* clang-format off */
    if (subzone.empty()) { return false; }
    /* clang-format on */

    const auto& timezone        = fmt::format(FMT_COMPILE("{}/{}"), zone, subzone);
    const auto& content         = fmt::format(FMT_COMPILE("\nSet Time Zone: {}?\n"), timezone);
    const auto& do_set_timezone = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    /* clang-format off */
    if (!do_set_timezone) { return false; }
    /* clang-format on */

    utils::set_timezone(timezone);

    return true;
}

// Set system clock type
void set_hw_clock() noexcept {
    auto screen = ScreenInteractive::Fullscreen();
    const std::vector<std::string> menu_entries{"utc", "localtime"};
    std::int32_t selected{};
    auto ok_callback = [&] {
        const auto& clock_type = menu_entries[static_cast<std::size_t>(selected)];
        utils::set_hw_clock(clock_type);
        screen.ExitLoopClosure()();
    };

    static constexpr auto hw_clock_body = "UTC is the universal time standard,\nand is recommended unless dual-booting with Windows."sv;
    auto content_size                   = size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, hw_clock_body, {std::move(content_size), size(HEIGHT, GREATER_THAN, 1)});
}

// Set password for root user
void set_root_password() noexcept {
    std::string pass{};
    static constexpr auto root_pass_body = "Enter Root password"sv;
    if (!detail::inputbox_widget(pass, root_pass_body, size(HEIGHT, GREATER_THAN, 1), true)) {
        return;
    }
    std::string confirm{};
    static constexpr auto root_confirm_body = "Re-enter Root password"sv;
    if (!detail::inputbox_widget(confirm, root_confirm_body, size(HEIGHT, GREATER_THAN, 1), true)) {
        return;
    }

    if (pass != confirm) {
        static constexpr auto PassErrBody = "\nThe passwords entered do not match.\nPlease try again.\n"sv;
        detail::msgbox_widget(PassErrBody);
        tui::set_root_password();
    }
    utils::set_root_password(pass);
}

// Create user on the system
void create_new_user() noexcept {
    std::string user{};
    static constexpr auto user_body = "Enter the user name. Letters MUST be lower case."sv;
    if (!detail::inputbox_widget(user, user_body, size(HEIGHT, GREATER_THAN, 1))) {
        return;
    }

    // Loop while username is blank, has spaces, or has capital letters in it.
    while (user.empty() || gucc::utils::contains(user, " ") || std::ranges::any_of(user, [](char ch) { return std::isupper(ch); })) {
        user.clear();
        static constexpr auto user_err_body = "An incorrect user name was entered. Please try again."sv;
        if (!detail::inputbox_widget(user, user_err_body, size(HEIGHT, GREATER_THAN, 1))) {
            return;
        }
    }

    std::string_view shell{};
    {
        static constexpr auto DefShell               = "\nChoose the default shell.\n"sv;
        static constexpr auto UseSpaceBar            = "Use [Spacebar] to de/select options listed."sv;
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
                shell = "/usr/bin/zsh"sv;
                break;
            case 1:
                shell = "/bin/bash"sv;
                break;
            case 2: {
                shell = "/usr/bin/fish"sv;
                break;
            }
            }
            screen.ExitLoopClosure()();
        };
        detail::radiolist_widget(radiobox_list, ok_callback, &selected, &screen, {.text = shells_options_body}, {.text_size = nothing});
    }

    // Enter password. This step will only be reached where the loop has been skipped or broken.
    std::string pass{};
    static constexpr auto user_pass_body = "Enter password for"sv;
    const auto& user_pass_body_content   = fmt::format(FMT_COMPILE("{} {}"), user_pass_body, user);
    if (!detail::inputbox_widget(pass, user_pass_body_content, size(HEIGHT, GREATER_THAN, 1), true)) {
        return;
    }
    std::string confirm{};
    static constexpr auto user_confirm_body = "Re-enter the password."sv;
    if (!detail::inputbox_widget(confirm, user_confirm_body, size(HEIGHT, GREATER_THAN, 1), true)) {
        return;
    }

    while (pass != confirm) {
        static constexpr auto PassErrBody = "\nThe passwords entered do not match.\nPlease try again.\n"sv;
        detail::msgbox_widget(PassErrBody);
        pass.clear();
        confirm.clear();
        if (!detail::inputbox_widget(pass, user_pass_body_content, size(HEIGHT, GREATER_THAN, 1), true)) {
            return;
        }
        if (!detail::inputbox_widget(confirm, user_confirm_body, size(HEIGHT, GREATER_THAN, 1), true)) {
            return;
        }
    }

    // create new user. This step will only be reached where the password loop has been skipped or broken.
    detail::infobox_widget("\nCreating User and setting groups...\n"sv);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    utils::create_new_user(user, pass, shell);
}

// Install pkgs from user input
void install_cust_pkgs() noexcept {
    std::string packages{};
    static constexpr auto content = "\nType any extra packages you would like to add, separated by spaces.\n \nFor example, to install Firefox, MPV, FZF:\nfirefox mpv fzf\n"sv;
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
    const auto& cmd        = (hostcache) ? "pacstrap"sv : "pacstrap -c"sv;
    detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("{} {} {} |& tee -a /tmp/pacstrap.log"), cmd, mountpoint, packages)});
#endif
}

void remove_pkgs() noexcept {
    std::string packages{};
    static constexpr auto content = "\nType any packages you would like to remove, separated by spaces.\n \nFor example, to remove Firefox, MPV, FZF:\nfirefox mpv fzf\n"sv;
    if (!detail::inputbox_widget(packages, content, size(HEIGHT, GREATER_THAN, 4))) {
        return;
    }
    utils::remove_pkgs(packages);
}

void chroot_interactive() noexcept {
    static constexpr auto chroot_return = "\nYou will now chroot into your installed system.\nYou can do changes almost as if you had booted into your installation.\n \nType \"exit\" to exit chroot.\n"sv;
    detail::infobox_widget(chroot_return);
    std::this_thread::sleep_for(std::chrono::seconds(1));

#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    const auto& cmd_formatted = fmt::format(FMT_COMPILE("arch-chroot {} bash"), mountpoint);
    gucc::utils::exec(cmd_formatted, true);
#else
    gucc::utils::exec("bash"sv, true);
#endif
}

void install_grub_uefi() noexcept {
    static constexpr auto content = "\nInstall UEFI Bootloader GRUB.\n"sv;
    const auto& do_install_uefi   = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    /* clang-format off */
    if (!do_install_uefi) { return; }
    /* clang-format on */

    std::string bootid{"cachyos"};
    if (gucc::utils::exec_checked("efibootmgr | cut -d\\  -f2 | grep -q -o cachyos")) {
        static constexpr auto bootid_content = "\nInput the name identify your grub installation. Choosing an existing name overwrites it.\n"sv;
        if (!detail::inputbox_widget(bootid, bootid_content, size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
            return;
        }
    }

    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& uefi_mount = std::get<std::string>(config_data["UEFI_MOUNT"]);

    utils::clear_screen();
    // Ask if user wishes to set Grub as the default bootloader and act accordingly
    static constexpr auto set_boot_default_body  = "Some UEFI firmware may not detect the bootloader unless it is set\nas default by copying its efi stub to"sv;
    static constexpr auto set_boot_default_body2 = "and renaming it to bootx64.efi.\n\nIt is recommended to do so unless already using a default bootloader,\nor where intending to use multiple bootloaders.\n\nSet bootloader as default?"sv;

    const auto& do_set_default_bootloader = detail::yesno_widget(fmt::format(FMT_COMPILE("\n{} {}/EFI/boot {}\n"), set_boot_default_body, uefi_mount, set_boot_default_body2), size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));

    utils::install_grub_uefi(bootid, do_set_default_bootloader);
    /* clang-format off */
    if (!do_set_default_bootloader) { return; }
    /* clang-format on */

    detail::infobox_widget("\nGrub has been set as the default bootloader.\n"sv);
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

void install_refind() noexcept {
    static constexpr auto content = "\nThis installs refind and configures it to automatically detect your kernels.\nNo support for encrypted /boot or intel microcode.\nThese require manual boot stanzas or using a different bootloader.\n"sv;
    const auto& do_install_uefi   = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    /* clang-format off */
    if (!do_install_uefi) { return; }
    /* clang-format on */

    utils::install_refind();

    detail::infobox_widget("\nRefind was succesfully installed\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

void install_limine() noexcept {
    static constexpr auto content = "\nThis installs Limine and configures it for Btrfs support.\nNo support for encrypted /boot.\n"sv;
    const auto& do_install_uefi   = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    /* clang-format off */
    if (!do_install_uefi) { return; }
    /* clang-format on */

    utils::install_limine();

    detail::infobox_widget("\nLimine was succesfully installed\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

void install_systemd_boot() noexcept {
    static constexpr auto content = "\nThis installs systemd-boot and generates boot entries\nfor the currently installed kernels.\nThis bootloader requires your kernels to be on the UEFI partition.\nThis is achieved by mounting the UEFI partition to /boot.\n"sv;
    const auto& do_install_uefi   = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    /* clang-format off */
    if (!do_install_uefi) { return; }
    /* clang-format on */

    utils::install_systemd_boot();

    detail::infobox_widget("\nSystemd-boot was installed\n"sv);
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

void uefi_bootloader() noexcept {
#ifdef NDEVENV
    // Ensure again that efivarfs is mounted
    static constexpr auto efi_path = "/sys/firmware/efi/"sv;
    if (fs::exists(efi_path) && fs::is_directory(efi_path)) {
        // Mount efivarfs if it is not already mounted
        const auto& mount_out = gucc::utils::exec("mount | grep /sys/firmware/efi/efivars");
        if (mount_out.empty() && (mount("efivarfs", "/sys/firmware/efi/efivars", "efivarfs", 0, "") != 0)) {
            perror("utils::uefi_bootloader");
            exit(1);
        }
    }
#endif

    static constexpr auto bootloaderInfo        = "Refind can be used standalone or in conjunction with other bootloaders as a graphical bootmenu.\nIt autodetects all bootable systems at boot time.\nGrub supports encrypted /boot partition and detects all bootable systems when you update your kernels.\nIt supports booting .iso files from a harddrive and automatic boot entries for btrfs snapshots.\nSystemd-boot is very light and simple and has little automation.\nIt autodetects windows, but is otherwise unsuited for multibooting.\nLimine is lightweight bootloader with Btrfs snapshots loading support."sv;
    const std::vector<std::string> menu_entries = {
        "grub",
        "refind",
        "systemd-boot",
        "limine"};

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        auto* config_instance           = Config::instance();
        auto& config_data               = config_instance->data();
        const auto& selected_bootloader = menu_entries[static_cast<std::size_t>(selected)];
        config_data["BOOTLOADER"]       = selected_bootloader;

        switch (selected) {
        case 0:
            tui::install_grub_uefi();
            break;
        case 1:
            tui::install_refind();
            break;
        case 2:
            tui::install_systemd_boot();
            break;
        case 3:
            tui::install_limine();
            break;
        }
        screen.ExitLoopClosure()();
    };

    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, bootloaderInfo);
}

void install_base() noexcept {
    static constexpr auto base_installed = "/mnt/.base_installed"sv;
    if (fs::exists(base_installed)) {
        static constexpr auto content = "\nA CachyOS Base has already been installed on this partition.\nProceed anyway?\n"sv;
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
    std::unique_ptr<bool[]> kernels_state{new bool[available_kernels.size()]{false}};

    auto screen = ScreenInteractive::Fullscreen();
    std::string packages{};
    auto ok_callback = [&] {
        packages = detail::from_checklist_string(available_kernels, kernels_state.get());
        if (!gucc::utils::exec_checked(fmt::format(FMT_COMPILE("echo '{}' | grep -q 'linux'"), packages))) {
            // Check if a kernel is already installed
            if (!gucc::utils::exec_checked(fmt::format(FMT_COMPILE("ls {}/boot/*.img >/dev/null 2>&1"), mountpoint))) {
                static constexpr auto ErrNoKernel = "\nAt least one kernel must be selected.\n"sv;
                detail::msgbox_widget(ErrNoKernel);
                return;
            }
            const auto& cmd = gucc::utils::exec(fmt::format(FMT_COMPILE("ls {}/boot/*.img | cut -d'-' -f2 | grep -v ucode.img | sort -u"), mountpoint));
            detail::msgbox_widget(fmt::format(FMT_COMPILE("\nlinux-{} detected\n"), cmd));
            screen.ExitLoopClosure()();
        }
        spdlog::info("selected: {}", packages);
        screen.ExitLoopClosure()();
    };

    static constexpr auto InstStandBseBody = "\nThe base package group will be installed automatically.\nThe base-devel package group is required to use the Arch User Repository (AUR).\n"sv;
    static constexpr auto UseSpaceBar      = "Use [Spacebar] to de/select options listed."sv;
    const auto& kernels_options_body       = fmt::format(FMT_COMPILE("\n{}{}\n"), InstStandBseBody, UseSpaceBar);

    static constexpr auto base_title = "New CLI Installer | Install Base"sv;
    detail::checklist_widget(available_kernels, ok_callback, kernels_state.get(), &screen, kernels_options_body, base_title, {.text_size = nothing});

    /* clang-format off */
    if (packages.empty()) { return; }
    /* clang-format on */

    config_data["KERNEL"] = packages;
    utils::install_base(packages);
}

void install_desktop() noexcept {
#ifdef NDEVENV
    static constexpr auto base_installed = "/mnt/.base_installed"sv;
    if (!fs::exists(base_installed)) {
        static constexpr auto content = "\nA CachyOS Base is not installed on this partition.\n"sv;
        detail::infobox_widget(content);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return;
    }
#endif

    // Prep variables

    // TODO(vnepogodin): make available DEs configurable or fetchable from net-profiles
    const std::vector<std::string> available_des{
        "kde",       // KDE Plasma
        "gnome",     // Gnome
        "xfce",      // Xfce4
        "bspwm",     // BSPWM (empty)
        "budgie",    // Budgie
        "cinnamon",  // Cinnamon
        "cosmic",    // Cosmic
        "i3wm",      // i3 Window Manager
        "hyprland",  // Hyprland
        "niri",      // Niri
        "lxde",      // LXDE
        "lxqt",      // LXQT
        "mate",      // Mate
        "openbox",   // Openbox
        "qtile",     // Qtile
        "sway",      // Sway
        "ukui",      // UKUI"
        "wayfire",   // Wayfire
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::string desktop_env{};
    std::int32_t selected{};
    auto ok_callback = [&] {
        desktop_env = available_des[static_cast<std::size_t>(selected)];
        spdlog::info("selected: {}", desktop_env);
        screen.ExitLoopClosure()();
    };

    static constexpr auto InstManDEBody = "\nPlease choose a desktop environment.\n"sv;
    static constexpr auto UseSpaceBar   = "Use [Spacebar] to de/select options listed."sv;
    const auto& des_options_body        = fmt::format(FMT_COMPILE("\n{}{}\n"), InstManDEBody, UseSpaceBar);

    static constexpr auto desktop_title = "New CLI Installer | Install Desktop"sv;
    detail::radiolist_widget(available_des, ok_callback, &selected, &screen, {des_options_body, desktop_title}, {.text_size = nothing});

    /* clang-format off */
    if (desktop_env.empty()) { return; }
    /* clang-format on */

    utils::install_desktop(desktop_env);
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

    static constexpr auto config_base_body = "Basic configuration of the base."sv;
    auto content_size                      = size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, config_base_body, {std::move(content_size), size(ftxui::HEIGHT, ftxui::GREATER_THAN, 1)});
}

// Grub auto-detects installed kernel
void bios_bootloader() {
    static constexpr auto bootloaderInfo        = "The installation device for GRUB can be selected in the next step."sv;
    const std::vector<std::string> menu_entries = {
        "grub",
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
    if (!tui::select_device()) { return; }
    /* clang-format on */

    auto* config_instance     = Config::instance();
    auto& config_data         = config_instance->data();
    config_data["BOOTLOADER"] = selected_bootloader;
    utils::bios_bootloader(selected_bootloader);
}

void enable_autologin() {
    // Detect display manager
    const auto& dm      = gucc::utils::exec("file /mnt/etc/systemd/system/display-manager.service 2>/dev/null | awk -F'/' '{print $NF}' | cut -d. -f1");
    const auto& content = fmt::format(FMT_COMPILE("\nThis option enables autologin using {}.\n\nProceed?\n"), dm);
    /* clang-format off */
    if (!detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75))) { return; }
    if (gucc::utils::exec("echo /mnt/home/* | xargs -n1 | wc -l") != "1"sv) { return; }
    /* clang-format on */

    const auto& autologin_user = gucc::utils::exec("echo /mnt/home/* | cut -d/ -f4");
    utils::enable_autologin(dm, autologin_user);
}

void tweaks_menu() noexcept {
    const std::vector<std::string> menu_entries = {
        "Enable Automatic Login",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            tui::enable_autologin();
            break;
        default:
            screen.ExitLoopClosure()();
            break;
        }
    };
    static constexpr auto tweaks_body = "Various configuration options"sv;
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, tweaks_body, {.text_size = size(HEIGHT, GREATER_THAN, 1)});
}

void install_bootloader() noexcept {
    /* clang-format off */
    if (!utils::check_base()) { return; }
    /* clang-format on */

    auto* config_instance   = Config::instance();
    auto& config_data       = config_instance->data();
    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    if (system_info == "BIOS"sv) {
        tui::bios_bootloader();
    } else {
        tui::uefi_bootloader();
    }
}

// BIOS and UEFI
void auto_partition() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& device_info = std::get<std::string>(config_data["DEVICE"]);
    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    auto& bootloader        = std::get<std::string>(config_data["BOOTLOADER"]);

    // If bootloader has not been selected yet, prompt user to select one.
    // This is needed because auto-partition uses the bootloader to determine
    // the boot mountpoint (/boot vs /boot/efi) and partition layout.
    if (bootloader.empty()) {
        const auto& menu_entries = [](const auto& bios_mode) -> std::vector<std::string> {
            const auto& available_bootloaders = utils::available_bootloaders(bios_mode);

            std::vector<std::string> result{};
            result.reserve(available_bootloaders.size());

            std::ranges::transform(available_bootloaders, std::back_inserter(result),
                [=](const std::string_view& entry) -> std::string { return std::string(entry); });
            return result;
        }(system_info);

        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        bool success{};
        auto ok_callback = [&] {
            bootloader = menu_entries[static_cast<std::size_t>(selected)];
            success    = true;
            screen.ExitLoopClosure()();
        };
        static constexpr auto select_bl_body = "\nSelect a bootloader for automatic partitioning.\nThis determines boot partition mountpoint and size.\n"sv;
        detail::menu_widget(menu_entries, ok_callback, &selected, &screen, select_bl_body);

        if (!success || bootloader.empty()) {
            spdlog::info("Bootloader selection cancelled, aborting auto-partition");
            return;
        }
        spdlog::info("Selected bootloader for partitioning: {}", bootloader);
    }

    // Generate and apply partition schema
    auto partitions = utils::auto_partition();
    if (partitions.empty()) {
        detail::msgbox_widget("\nAutomatic partitioning failed. Check the log for details.\n");
        return;
    }

    // Save partition layout for user confirmation
    config_data["PARTITION_SCHEMA"] = partitions;
    spdlog::info("Auto-partition schema stored with {} entries", partitions.size());

    // Show created partitions
    const auto& disk_list = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk {} -o NAME,TYPE,FSTYPE,SIZE"), device_info));
    detail::msgbox_widget(disk_list, size(HEIGHT, GREATER_THAN, 5));
}

// Simple code to show devices / partitions.
void show_devices() noexcept {
    const auto& lsblk = gucc::utils::exec(R"(lsblk -o NAME,MODEL,TYPE,FSTYPE,SIZE,MOUNTPOINT | grep "disk\|part\|lvm\|crypt\|NAME\|MODEL\|TYPE\|FSTYPE\|SIZE\|MOUNTPOINT")");
    detail::msgbox_widget(lsblk, size(HEIGHT, GREATER_THAN, 5));
}

// Refresh pacman keys
void refresh_pacman_keys() noexcept {
#ifdef NDEVENV
    utils::arch_chroot("bash -c 'pacman-key --init;pacman-key --populate archlinux cachyos;pacman-key --refresh-keys;'");
#else
    SPDLOG_DEBUG("({}) Function is doing nothing in dev environment!", __PRETTY_FUNCTION__);
#endif
}

// This function does not assume that the formatted device is the Root installation device as
// more than one device may be formatted. Root is set in the mount_partitions function.
bool select_device() noexcept {
    auto* config_instance    = Config::instance();
    auto& config_data        = config_instance->data();
    auto devices             = gucc::utils::exec(R"(lsblk -lno NAME,SIZE,TYPE | grep 'disk' | awk '{print "/dev/" $1 " " $2}' | sort -u)");
    const auto& devices_list = gucc::utils::make_multiline(devices);

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    bool success{};
    auto ok_callback = [&] {
        const auto& src       = devices_list[static_cast<std::size_t>(selected)];
        const auto& lines     = gucc::utils::make_multiline(src, false, ' ');
        config_data["DEVICE"] = lines[0];
        success               = true;
        screen.ExitLoopClosure()();
    };
    detail::menu_widget(devices_list, ok_callback, &selected, &screen);

    return success;
}

// Helper function to select filesystem
auto select_fs_and_cmd() noexcept -> std::optional<std::pair<std::string, std::string>> {
    using FsCmdPair = std::pair<std::string, std::string>;

    const std::vector<FsCmdPair> menu_entries = {
        {"skip", "Do not format -"},
        {"btrfs", "btrfs mkfs.btrfs -f"},
        {"ext4", "ext4 mkfs.ext4 -q"},
        {"f2fs", "f2fs mkfs.f2fs -q"},
        {"xfs", "xfs mkfs.xfs -f"},
    };

    std::vector<std::string> menu_display_entries;
    menu_display_entries.reserve(menu_entries.size());
    for (const auto& entry : menu_entries) {
        menu_display_entries.push_back(entry.second);
    }

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    std::optional<FsCmdPair> result{};
    auto ok_callback = [&] {
        const auto& [fstype, cmd_entry] = menu_entries[static_cast<std::size_t>(selected)];

        // strip fs name of the entry
        const auto& mkfs_command = cmd_entry.substr(cmd_entry.find_first_of(' ') + 1, cmd_entry.size());

        result = {{fstype, (fstype == "skip"sv) ? ""s : mkfs_command}};
        screen.ExitLoopClosure()();
    };
    detail::menu_widget(menu_display_entries, ok_callback, &selected, &screen);

    return result;
}

// Set static list of filesystems rather than on-the-fly. Partially as most require additional flags, and
// partially because some don't seem to be viable.
bool select_filesystem() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    // prep variables
    config_data["fs_opts"] = std::vector<std::string>{};

    // 1. Select filesystem type and format command (or skip)
    auto fs_selection = select_fs_and_cmd();
    /* clang-format off */
    if (!fs_selection) { return false; }
    if (fs_selection->first == "skip"sv) { return true; }
    /* clang-format on */

    // 2. Take actions

    // set all variables
    utils::select_filesystem(fs_selection->first);

    // Warn about formatting!
    const auto& file_sys  = std::get<std::string>(config_data["FILESYSTEM"]);
    const auto& partition = std::get<std::string>(config_data["PARTITION"]);
    const auto& content   = fmt::format(FMT_COMPILE("\nMount {}\n \n! Data on {} will be lost !\n"), file_sys, partition);
    const auto& do_mount  = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    if (do_mount) {
#ifdef NDEVENV
        gucc::utils::exec(fmt::format(FMT_COMPILE("{} {}"), file_sys, partition));
#endif
        spdlog::info("mount.{} {}", partition, file_sys);
    }

    return true;
}

auto select_mount_opts(std::string_view partition, std::string_view fstype, bool force = false) noexcept -> std::string {
    const bool is_ssd    = gucc::disk::is_device_ssd(partition);
    const auto& fs_type  = gucc::fs::string_to_filesystem_type(fstype);
    auto mount_opts_info = gucc::fs::get_default_mount_opts(fs_type, is_ssd);
    if (force) {
        return mount_opts_info;
    }

    // Show an input box with the default mount opts pre-filled
    static constexpr auto mount_options_body = "\nEdit mount options for the partition.\nThe defaults are pre-filled based on filesystem and device type.\n"sv;
    if (!detail::inputbox_widget(mount_opts_info, mount_options_body, size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
        return ""s;
    }
    return std::string{gucc::utils::trim(mount_opts_info)};
}

// This subfunction allows for special mounting options to be applied for relevant fs's.
// Separate subfunction for neatness.
void mount_opts(bool force) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& fstype    = std::get<std::string>(config_data["FILESYSTEM_NAME"]);
    const auto& partition = std::get<std::string>(config_data["PARTITION"]);

    // select mount opts
    auto& mount_opts_info = std::get<std::string>(config_data["MOUNT_OPTS"]);
    mount_opts_info       = select_mount_opts(partition, fstype, force);
}

bool mount_current_partition(bool force) noexcept {
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& partition  = std::get<std::string>(config_data["PARTITION"]);
    const auto& mount_dev  = std::get<std::string>(config_data["MOUNT"]);

    config_data["MOUNT_OPTS"] = "";
    /* clang-format off */
    // Get mounting options for appropriate filesystems
    const auto& fs_opts = std::get<std::vector<std::string>>(config_data["fs_opts"]);
    if (!fs_opts.empty()) { tui::mount_opts(force); }
    /* clang-format on */

    const auto& mount_dir  = fmt::format(FMT_COMPILE("{}{}"), mountpoint, mount_dev);
    const auto& mount_opts = std::get<std::string>(config_data["MOUNT_OPTS"]);
    if (!utils::mount_partition(partition, mountpoint, mount_dev, mount_opts)) {
        spdlog::error("Failed to mount current partition {}", partition);
    }

    return confirm_mount(mount_dir, force);
}

void make_swap(std::vector<gucc::fs::Partition>& partition_schema) noexcept {
    static constexpr auto sel_swap_body = "\nSelect SWAP Partition.\nIf using a Swapfile, it will initially set the same size as your RAM.\n"sv;
    static constexpr auto sel_swap_none = "None"sv;
    static constexpr auto sel_swap_file = "Swapfile"sv;

    auto* config_instance       = Config::instance();
    auto& config_data           = config_instance->data();
    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);

    std::string answer{};
    {
        std::vector<std::string> temp{"None -"};
        const auto& root_filesystem = gucc::fs::utils::get_mountpoint_fs(mountpoint_info);
        if (!(root_filesystem == "zfs"sv || root_filesystem == "btrfs"sv)) {
            temp.emplace_back("Swapfile -");
        }
        const auto& partitions = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);
        temp.reserve(partitions.size());
        std::ranges::copy(partitions, std::back_inserter(temp));

        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        bool success{};
        auto ok_callback = [&] {
            const auto& src   = temp[static_cast<std::size_t>(selected)];
            const auto& lines = gucc::utils::make_multiline(src, false, ' ');
            answer            = lines[0];
            success           = true;
            screen.ExitLoopClosure()();
        };
        /* clang-format off */
        detail::menu_widget(temp, ok_callback, &selected, &screen, sel_swap_body, {.text_size = size(HEIGHT, GREATER_THAN, 1)});
        if (!success) { return; }
        /* clang-format on */
    }

    auto& partition = std::get<std::string>(config_data["PARTITION"]);
    /* clang-format off */
    if (answer == sel_swap_none) { return; }
    partition = answer;
    /* clang-format on */

    if (partition == sel_swap_file) {
        const auto& total_memory = gucc::utils::exec("grep MemTotal /proc/meminfo | awk '{print $2/1024}' | sed 's/\\..*//'");
        std::string value{fmt::format(FMT_COMPILE("{}M"), total_memory)};
        if (!detail::inputbox_widget(value, "\nM = MB, G = GB\n", size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
            return;
        }

        while (!gucc::utils::exec_checked(fmt::format(FMT_COMPILE("echo '{}' | grep -q 'M\\|G'"), value))) {
            detail::msgbox_widget(fmt::format(FMT_COMPILE("\n{} Error: M = MB, G = GB\n"), sel_swap_file));
            value = fmt::format(FMT_COMPILE("{}M"), total_memory);
            if (!detail::inputbox_widget(value, "\nM = MB, G = GB\n", size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
                return;
            }
        }

#ifdef NDEVENV
        const auto& swapfile_path = fmt::format(FMT_COMPILE("{}/swapfile"), mountpoint_info);
        if (!gucc::swap::make_swapfile(mountpoint_info, value)) {
            spdlog::error("Failed to create swapfile: {}", swapfile_path);
            return;
        }

        config_data["SWAP_DEVICE"] = swapfile_path;
#endif
        return;
    }

    auto& partitions        = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);
    auto& number_partitions = std::get<std::int32_t>(config_data["NUMBER_PARTITIONS"]);

    // TODO(vnepogodin): handle swap partition mount options?
    auto swap_mountopts = gucc::fs::get_default_mount_opts(gucc::fs::FilesystemType::LinuxSwap, false);
    auto swap_partition = gucc::fs::Partition{.fstype = "linuxswap"s, .mountpoint = ""s, .device = partition, .mount_opts = std::move(swap_mountopts)};

    // Warn user if creating a new swap
    const auto& swap_part = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -o FSTYPE '{}' | grep -i 'swap'"), partition));
    if (swap_part != "swap"sv) {
        const auto& do_swap = detail::yesno_widget(fmt::format(FMT_COMPILE("\nmkswap {}\n"), partition), size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
        /* clang-format off */
        if (!do_swap) { return; }
        /* clang-format on */
    }

#ifdef NDEVENV
    if (!gucc::swap::make_swap_partition(swap_partition)) {
        spdlog::error("Failed to create swap parition");
        return;
    }
    config_data["SWAP_DEVICE"] = partition;
#endif

    const auto& part_uuid   = gucc::fs::utils::get_device_uuid(partition);
    swap_partition.uuid_str = part_uuid;

    // TODO(vnepogodin): handle luks information

    utils::dump_partition_to_log(swap_partition);

    // insert swap partition
    partition_schema.emplace_back(std::move(swap_partition));

    // Since a partition was used, remove that partition from the list
    std::erase_if(partitions, [partition](std::string_view x) { return gucc::utils::contains(x, partition); });
    number_partitions -= 1;
}

void lvm_detect() noexcept {
    utils::lvm_detect([] {
        detail::infobox_widget("\nExisting Logical Volume Management (LVM) detected.\nActivating. Please Wait...\n"sv);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    });
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
    static constexpr auto del_lvmvg_body = "\nSelect Volume Group to delete.\nAll Logical Volumes within will also be deleted.\n"sv;
    detail::menu_widget(vg_list, ok_callback, &selected, &screen, del_lvmvg_body);
    if (sel_vg.empty()) { return; }
    /* clang-format on */

    // Ask for confirmation
    const auto& do_action = detail::yesno_widget("\nConfirm deletion of Volume Group(s) and Logical Volume(s).\n"sv);
    /* clang-format off */
    if (!do_action) { return; }
    /* clang-format on */

#ifdef NDEVENV
    // if confirmation given, delete
    gucc::utils::exec(fmt::format(FMT_COMPILE("vgremove -f {} 2>/dev/null"), sel_vg), true);
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

    static constexpr auto lvm_menu_body = "\nLogical Volume Management (LVM) allows 'virtual' hard drives (Volume Groups)\nand partitions (Logical Volumes) to be created from existing drives\nand partitions. A Volume Group must be created first, then one or more\nLogical Volumes in it.\n \nLVM can also be used with an encrypted partition to create multiple logical\nvolumes (e.g. root and home) in it.\n"sv;
    auto text_size                      = size(HEIGHT, LESS_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, lvm_menu_body, {size(HEIGHT, LESS_THAN, 18), std::move(text_size)});
}

// creates a new zpool on an existing partition
bool zfs_create_zpool(bool do_create_zpool = true) noexcept {
    // LVM Detection. If detected, activate.
    tui::lvm_detect();

    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    config_data["INCLUDE_PART"] = "part\\|lvm\\|crypt";
    utils::umount_partitions();
    utils::find_partitions();

    // Filter out partitions that have already been mounted and partitions that just contain crypt or zfs devices
    auto ignore_part = utils::list_mounted();
    ignore_part += gucc::fs::zfs_list_devs();
    ignore_part += utils::list_containing_crypt();

    /* const auto& parts = gucc::utils::make_multiline(ignore_part);
    for (const auto& part : parts) {
        utils::delete_partition_in_list(part);
    }*/

    // Identify the partition for the zpool
    {
        const auto& partitions = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);

        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        bool success{};
        auto ok_callback = [&] {
            const auto& src          = partitions[static_cast<std::size_t>(selected)];
            const auto& lines        = gucc::utils::make_multiline(src, false, ' ');
            config_data["PARTITION"] = lines[0];
            success                  = true;
            screen.ExitLoopClosure()();
        };

        static constexpr auto zfs_zpool_partmenu_body = "\nSelect a partition to hold the ZFS zpool\n"sv;
        auto text_size                                = size(HEIGHT, LESS_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
        detail::menu_widget(partitions, ok_callback, &selected, &screen, zfs_zpool_partmenu_body, {size(HEIGHT, LESS_THAN, 18), std::move(text_size)});
        /* clang-format off */
        if (!success) { return false; }
        /* clang-format on */
    }

    static constexpr auto zfs_zpool_body        = "\nEnter the name for the new zpool\n"sv;
    static constexpr auto zfs_zpoolcvalidation1 = "\nzpool names must start with a letter and are limited to only alphanumeric characters and the special characters : . - _\n"sv;
    static constexpr auto zfs_zpoolcvalidation2 = "\nzpool names cannot start with the reserved words (log, mirror, raidz, raidz1, raidz2, raidz3, or spare)\n"sv;

    // We need to get a name for the zpool
    std::string zfs_zpool_name{"zpcachyos"};
    auto zfs_menu_text = zfs_zpool_body;

    // Loop while zpool name is not valid.
    while (true) {
        if (!detail::inputbox_widget(zfs_zpool_name, zfs_menu_text, size(HEIGHT, GREATER_THAN, 1))) {
            return false;
        }
        zfs_menu_text = zfs_zpool_body;

        // validation
        if (zfs_zpool_name.empty() || std::isdigit(zfs_zpool_name[0]) || (std::ranges::any_of(zfs_zpool_name, [](char ch) { return (!std::isalnum(ch)) && (ch != ':') && (ch != '.') && (ch != '-') && (ch != '_'); }))) {
            zfs_menu_text = zfs_zpoolcvalidation1;
        }

        for (auto&& invalid_keyword : {"log", "mirror", "raidz", "raidz1", "raidz2", "spare"}) {
            if (gucc::utils::contains(zfs_zpool_name, invalid_keyword)) {
                zfs_menu_text = zfs_zpoolcvalidation2;
                break;
            }
        }
        /* clang-format off */
        if (zfs_menu_text == zfs_zpool_body) { break; }
        /* clang-format on */
    }

    // save zpool name
    config_data["ZFS_ZPOOL_NAME"] = zfs_zpool_name;

    /* clang-format off */
    if (!do_create_zpool) { return true; }
    /* clang-format on */

    const auto& partition = std::get<std::string>(config_data["PARTITION"]);
    return utils::zfs_create_zpool(partition, zfs_zpool_name);
}

bool zfs_import_pool() noexcept {
    const auto& zlist = gucc::utils::make_multiline(gucc::utils::exec("zpool import 2>/dev/null | grep '^[[:space:]]*pool' | awk -F : '{print $2}' | awk '{$1=$1};1'"));
    if (zlist.empty()) {
        // no available datasets
        detail::infobox_widget("\nNo pools available\n"sv);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return false;
    }

    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    std::string zfs_zpool_name{};
    {
        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        bool success{};
        auto ok_callback = [&] {
            zfs_zpool_name = zlist[static_cast<std::size_t>(selected)];
            success        = true;
            screen.ExitLoopClosure()();
        };
        static constexpr auto zfs_menu_body = "\nSelect a zpool from the list\n"sv;
        auto text_size                      = size(HEIGHT, LESS_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
        detail::menu_widget(zlist, ok_callback, &selected, &screen, zfs_menu_body, {size(HEIGHT, LESS_THAN, 18), std::move(text_size)});
        /* clang-format off */
        if (!success) { return false; }
        /* clang-format on */
    }

#ifdef NDEVENV
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    gucc::utils::exec(fmt::format(FMT_COMPILE("zpool import -R {} {} 2>>/tmp/cachyos-install.log"), mountpoint, zfs_zpool_name), true);
#endif

    config_data["ZFS"] = 1;

    return true;
}

bool zfs_new_ds(const std::string_view& zmount = "") noexcept {
    const auto& zlist = gucc::utils::make_multiline(gucc::fs::zfs_list_pools());
    if (zlist.empty()) {
        // no available datasets
        detail::infobox_widget("\nNo pools available\n"sv);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return false;
    }

    std::string zfs_zpool_name{};
    {
        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        bool success{};
        auto ok_callback = [&] {
            zfs_zpool_name = zlist[static_cast<std::size_t>(selected)];
            success        = true;
            screen.ExitLoopClosure()();
        };
        static constexpr auto zfs_menu_body = "\nSelect a zpool from the list\n"sv;
        auto text_size                      = size(HEIGHT, LESS_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
        detail::menu_widget(zlist, ok_callback, &selected, &screen, zfs_menu_body, {size(HEIGHT, LESS_THAN, 18), std::move(text_size)});
        /* clang-format off */
        if (!success) { return false; }
        /* clang-format on */
    }

    static constexpr auto zfs_dataset_body      = "\nEnter a name and relative path for the dataset.\n \nFor example, if you want the dataset to be placed at zpool/data/zname, enter 'data/zname'\n"sv;
    static constexpr auto zfs_zpoolcvalidation1 = "\nzpool names must start with a letter and are limited to only alphanumeric characters and the special characters : . - _\n"sv;

    // We need to get a name for the dataset
    std::string zfs_dataset_name{};
    auto zfs_menu_text = zfs_dataset_body;

    // Loop while zpool name is not valid.
    while (true) {
        if (!detail::inputbox_widget(zfs_dataset_name, zfs_menu_text, size(HEIGHT, GREATER_THAN, 1))) {
            return false;
        }
        zfs_menu_text = zfs_dataset_body;

        // validation
        if (zfs_dataset_name.empty() || std::isdigit(zfs_dataset_name[0]) || (std::ranges::any_of(zfs_dataset_name, [](char ch) { return (!std::isalnum(ch)) && (ch != '/') && (ch != ':') && (ch != '.') && (ch != '-') && (ch != '_'); }))) {
            zfs_menu_text = zfs_zpoolcvalidation1;
        }

        /* clang-format off */
        if (zfs_menu_text == zfs_dataset_body) { break; }
        /* clang-format on */
    }

    const auto& zfs_zpath = fmt::format(FMT_COMPILE("{}/{}"), zfs_zpool_name, zfs_dataset_name);
    if (zmount == "legacy"sv) {
        if (!gucc::fs::zfs_create_dataset(zfs_zpath, zmount)) {
            spdlog::error("Failed to create zfs dataset {} at mountpoint {}", zfs_zpath, zmount);
        }
    } else if (zmount == "zvol"sv) {
        static constexpr auto zvol_size_menu_body       = "\nEnter the size of the zvol in megabytes(MB)\n"sv;
        static constexpr auto zvol_size_menu_validation = "\nYou must enter a number greater than 0\n"sv;

        // We need to get a name for the zvol
        std::string zvol_size{};
        zfs_menu_text = zvol_size_menu_body;

        // Loop while zvol name is not valid.
        while (true) {
            if (!detail::inputbox_widget(zvol_size, zfs_menu_text, size(HEIGHT, GREATER_THAN, 1))) {
                return false;
            }
            zfs_menu_text = zvol_size_menu_body;

            // validation

            if (zvol_size.empty() || std::ranges::any_of(zvol_size, [](char ch) { return !std::isdigit(ch); })) {
                zfs_menu_text = zvol_size_menu_validation;
            }

            /* clang-format off */
            if (zfs_menu_text == zvol_size_menu_body) { break; }
            /* clang-format on */
        }
        if (!gucc::fs::zfs_create_zvol(zvol_size, zfs_zpath)) {
            spdlog::error("Failed to create zfs zvol {} with size {}", zfs_zpath, zvol_size);
        }
    } else {
        spdlog::error("HELLO! IMPLEMENT ME!");
        return false;
    }

    return true;
}

void zfs_set_property() noexcept {
    const auto& zlist = gucc::utils::make_multiline(gucc::fs::zfs_list_datasets());
    if (zlist.empty()) {
        // no available datasets
        detail::infobox_widget("\nNo datasets available\n"sv);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return;
    }

    std::string zdataset{};
    {
        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        bool success{};
        auto ok_callback = [&] {
            zdataset = zlist[static_cast<std::size_t>(selected)];
            success  = true;
            screen.ExitLoopClosure()();
        };
        static constexpr auto zfs_menu_body = "\nSelect the dataset you would like to set a property on\n"sv;
        auto text_size                      = size(HEIGHT, LESS_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
        detail::menu_widget(zlist, ok_callback, &selected, &screen, zfs_menu_body, {size(HEIGHT, LESS_THAN, 18), std::move(text_size)});
        /* clang-format off */
        if (!success) { return; }
        /* clang-format on */
    }

    static constexpr auto zfs_mountpoint_body  = "\nEnter the property and value you would like to\nset using the format property=mountpoint\n \nFor example, you could enter:\ncompression=lz4\nor\nacltype=posixacl\n\n"sv;
    static constexpr auto zfs_property_invalid = "\nInput must be the format property=mountpoint\n"sv;

    // We need to get a valid property
    std::string zfs_property_ent{};
    auto zfs_menu_text = zfs_mountpoint_body;

    // Loop while property is not valid.
    while (true) {
        if (!detail::inputbox_widget(zfs_property_ent, zfs_menu_text, size(HEIGHT, GREATER_THAN, 1))) {
            return;
        }
        zfs_menu_text = zfs_mountpoint_body;

        // validation
        if (!ctre::match<"[a-zA-Z]*=[a-zA-Z0-9]*">(zfs_property_ent)) {
            zfs_menu_text = zfs_property_invalid;
        }

        /* clang-format off */
        if (zfs_menu_text == zfs_mountpoint_body) { break; }
        /* clang-format on */
    }

    // Set the property
    if (!gucc::fs::zfs_set_property(zfs_property_ent, zdataset)) {
        spdlog::error("Failed to set zfs property '{}' on dataset '{}'", zfs_property_ent, zdataset);
    }
}

void zfs_destroy_dataset() noexcept {
    const auto& zlist = gucc::utils::make_multiline(gucc::fs::zfs_list_datasets());
    if (zlist.empty()) {
        // no available datasets
        detail::infobox_widget("\nNo datasets available\n"sv);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return;
    }

    std::string zdataset{};
    {
        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        bool success{};
        auto ok_callback = [&] {
            zdataset = zlist[static_cast<std::size_t>(selected)];
            success  = true;
            screen.ExitLoopClosure()();
        };
        static constexpr auto zfs_destroy_menu_body = "\nSelect the dataset you would like to permanently delete.\nPlease note that this will recursively delete any child datasets with warning\n"sv;
        auto text_size                              = size(HEIGHT, LESS_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
        detail::menu_widget(zlist, ok_callback, &selected, &screen, zfs_destroy_menu_body, {size(HEIGHT, LESS_THAN, 18), std::move(text_size)});
        /* clang-format off */
        if (!success) { return; }
        /* clang-format on */
    }

    const auto& content    = fmt::format(FMT_COMPILE("\nPlease confirm that you want to irrevocably\ndelete all the data on '{}'\nand the data contained on all of it's children\n"), zdataset);
    const auto& do_destroy = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 20) | size(WIDTH, LESS_THAN, 75));
    /* clang-format off */
    if (!do_destroy) { return; }
    /* clang-format on */

    gucc::fs::zfs_destroy_dataset(zdataset);
}

// Automated configuration of zfs. Creates a new zpool and a default set of filesystems
void zfs_auto() noexcept {
    auto* config_instance      = Config::instance();
    auto& config_data          = config_instance->data();
    const auto& partition      = std::get<std::string>(config_data["PARTITION"]);
    const auto& zfs_zpool_name = std::get<std::string>(config_data["ZFS_ZPOOL_NAME"]);

    // it creates using our preset, so don't ask for zpool name
    if (!utils::zfs_auto_pres(partition, zfs_zpool_name)) {
        detail::infobox_widget("\nOperation failed\n"sv);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return;
    }

    // provide confirmation to the user
    detail::infobox_widget("\nAutomatic zfs provisioning has been completed\n"sv);
    std::this_thread::sleep_for(std::chrono::seconds(3));
}

void zfs_menu_manual() noexcept {
    const std::vector<std::string> menu_entries = {
        "Create a new zpool",
        "Import an existing zpool",
        "Create and mount a ZFS filesystem",
        "Create a legacy ZFS filesystem",
        "Create a new ZVOL",
        "Set a property on a zfs filesystem",
        "Destroy a ZFS dataset",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            tui::zfs_create_zpool();
            break;
        case 1:
            tui::zfs_import_pool();
            break;
        case 2:
            tui::zfs_new_ds();
            break;
        case 3:
            tui::zfs_new_ds("legacy"sv);
            break;
        case 4:
            tui::zfs_new_ds("zvol"sv);
            break;
        case 5:
            tui::zfs_set_property();
            break;
        case 6:
            tui::zfs_destroy_dataset();
            break;
        default:
            screen.ExitLoopClosure()();
            return;
        }
    };

    static constexpr auto zfs_menu_manual_body = "\nPlease select an option below\n"sv;
    auto text_size                             = size(HEIGHT, LESS_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, zfs_menu_manual_body, {size(HEIGHT, LESS_THAN, 18), std::move(text_size)});
}

// The main ZFS menu
void zfs_menu() noexcept {
#ifdef NDEVENV
    // check for zfs support
    if (!gucc::utils::exec_checked("modprobe zfs 2>>/tmp/cachyos-install.log &>/dev/null")) {
        detail::infobox_widget("\nThe kernel modules to support ZFS could not be found\n"sv);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return;
    }
#endif

    const std::vector<std::string> menu_entries = {
        "Automatically configure",
        "Manual configuration",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            tui::zfs_auto();
            break;
        case 1:
            tui::zfs_menu_manual();
            break;
        default:
            screen.ExitLoopClosure()();
            return;
        }
    };

    static constexpr auto zfs_menu_body = "\nZFS is a flexible and resilient file system that combines elements of\nlogical volume management, RAID and traditional file systems.\nZFS on Linux requires special handling and is not ideal for beginners.\n \nSelect automatic to select a partition and allow\nthe system to automate the creation a new a zpool and datasets\nmounted to '/', '/home' and '/var/cache/pacman'.\nManual configuration is available but requires specific knowledge of zfs.\n"sv;
    auto text_size                      = size(HEIGHT, LESS_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, zfs_menu_body, {size(HEIGHT, LESS_THAN, 18), std::move(text_size)});
}

void make_esp(std::vector<gucc::fs::Partition>& partitions) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    const auto& sys_info  = std::get<std::string>(config_data["SYSTEM"]);

    /* clang-format off */
    if (sys_info != "UEFI"sv) { return; }
    /* clang-format on */

    std::string answer{};
    {
        const auto& partitions_list = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);

        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        bool success{};
        auto ok_callback = [&] {
            const auto& src   = partitions_list[static_cast<std::size_t>(selected)];
            const auto& lines = gucc::utils::make_multiline(src, false, ' ');
            answer            = lines[0];
            success           = true;
            screen.ExitLoopClosure()();
        };

        /* clang-format off */
        static constexpr auto esp_part_body = "\nSelect BOOT partition.\n"sv;
        detail::menu_widget(partitions_list, ok_callback, &selected, &screen, esp_part_body, {.text_size = size(HEIGHT, GREATER_THAN, 1)});
        if (!success) { return; }
        /* clang-format on */
    }

    const auto& luks          = std::get<std::int32_t>(config_data["LUKS"]);
    auto& partition           = std::get<std::string>(config_data["PARTITION"]);
    partition                 = answer;
    config_data["UEFI_MOUNT"] = "";
    config_data["UEFI_PART"]  = partition;

    // If it is already a fat/vfat partition...
    bool do_boot_partition{};
    const auto& is_fat_part = gucc::utils::exec_checked(fmt::format(FMT_COMPILE("fsck -N {} | grep -q fat"), partition));
    if (!is_fat_part) {
        const auto& content = fmt::format(FMT_COMPILE("\nThe UEFI partition {} has already been formatted.\n \nReformat? Doing so will erase ALL data already on that partition.\n"), partition);
        do_boot_partition   = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    }
    if (do_boot_partition) {
#ifdef NDEVENV
        gucc::utils::exec(fmt::format(FMT_COMPILE("mkfs.vfat -F32 {} &>/dev/null"), partition));
#endif
        spdlog::debug("Formating boot partition with fat/vfat!");
    }

    {
        static constexpr auto MntUefiBody            = "\nSelect UEFI Mountpoint.\n \n/boot/efi is recommended for multiboot systems.\n/boot is required for systemd-boot.\n"sv;
        static constexpr auto MntUefiCrypt           = "\nSelect UEFI Mountpoint.\n \n/boot/efi is recommended for multiboot systems and required for full disk encryption.\nEncrypted /boot is supported only by grub and can lead to slow startup.\n \n/boot is required for systemd-boot and for refind when using encryption.\n"sv;
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
        detail::radiolist_widget(radiobox_list, ok_callback, &selected, &screen, {.text = MntUefiMessage}, {.text_size = nothing});
    }

    /* clang-format off */
    if (answer.empty()) { return; }
    /* clang-format on */

    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    auto& uefi_mount            = std::get<std::string>(config_data["UEFI_MOUNT"]);
    uefi_mount                  = answer;

    const auto& part_mountpoint = fmt::format(FMT_COMPILE("{}{}"), mountpoint_info, uefi_mount);
#ifdef NDEVENV
    gucc::utils::exec(fmt::format(FMT_COMPILE("mkdir -p {}"), part_mountpoint));
    gucc::utils::exec(fmt::format(FMT_COMPILE("mount {} {}"), partition, part_mountpoint));
#endif
    confirm_mount(part_mountpoint);

    // Use GUCC default mount opts for vfat
    const auto& part_fs    = gucc::fs::utils::get_mountpoint_fs(part_mountpoint);
    const bool is_boot_ssd = gucc::disk::is_device_ssd(partition);
    const auto& boot_opts  = gucc::fs::get_default_mount_opts(gucc::fs::FilesystemType::Vfat, is_boot_ssd);
    auto boot_part_struct  = gucc::fs::Partition{.fstype = part_fs, .mountpoint = uefi_mount, .device = partition, .mount_opts = boot_opts};

    const auto& part_uuid     = gucc::fs::utils::get_device_uuid(boot_part_struct.device);
    boot_part_struct.uuid_str = part_uuid;

    // TODO(vnepogodin): handle luks information

    utils::dump_partition_to_log(boot_part_struct);

    // insert boot partition
    partitions.emplace_back(std::move(boot_part_struct));
}

// Function to gather user choices for the root partition
auto select_root_partition_info() noexcept -> std::optional<utils::RootPartitionSelection> {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    utils::RootPartitionSelection selection{};

    // 1. Identify root partition device
    {
        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        bool success{};
        const auto& partitions_lines = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);

        auto ok_callback = [&] {
            const auto& src   = partitions_lines[static_cast<std::size_t>(selected)];
            const auto& lines = gucc::utils::make_multiline(src, false, ' ');
            selection.device  = lines[0];
            success           = true;
            screen.ExitLoopClosure()();
        };
        /* clang-format off */
        static constexpr auto sel_root_body = "\nSelect ROOT Partition.\nThis is where CachyOS will be installed.\n"sv;
        detail::menu_widget(partitions_lines, ok_callback, &selected, &screen, sel_root_body, {.text_size = size(HEIGHT, GREATER_THAN, 1)});
        if (!success) { return std::nullopt; }
        /* clang-format on */
    }
    spdlog::info("Selected root partition device: {}", selection.device);

    // NOTE: keep this for backward compatibility?
    config_data["PARTITION"] = selection.device;
    config_data["ROOT_PART"] = selection.device;
    // Reset the mountpoint variable, in case this is the second time through this menu and old state is still around
    config_data["MOUNT"] = "";

    // 2. Select filesystem type and format command (or skip)
    auto fs_selection = select_fs_and_cmd();
    if (!fs_selection) {
        return std::nullopt;
    }

    auto [fstype, mkfs_cmd]    = std::move(*fs_selection);
    selection.fstype           = std::move(fstype);
    selection.mkfs_command     = std::move(mkfs_cmd);
    selection.format_requested = !selection.mkfs_command.empty();

    spdlog::info("Selected filesystem type: {}, Format requested: {}",
        selection.fstype == "skip"sv ? "(skipped)"sv : selection.fstype, selection.format_requested);

    // 3. Select mount options if a filesystem was chosen
    if (selection.fstype != "skip"sv) {
        selection.mount_opts = select_mount_opts(selection.device, selection.fstype);
        spdlog::info("Selected mount options: '{}'", selection.mount_opts.empty() ? "(defaults)" : selection.mount_opts);
    } else {
        selection.mount_opts = select_mount_opts(selection.device, selection.fstype, true);
        spdlog::info("Using default mount options for existing filesystem.");
    }

    return selection;
}

// Formats and mounts root partition
auto mount_root_partition(std::vector<gucc::fs::Partition>& partitions) noexcept -> bool {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& zfs_zpool_names = std::get<std::vector<std::string>>(config_data["ZFS_ZPOOL_NAMES"]);
    if (!zfs_zpool_names.empty()) {
        // NOTE: assuming we only support single zpool
        const auto& zfs_zpool_name = zfs_zpool_names[0];
#ifdef NDEVENV
        const auto& zfs_import_cmd = fmt::format(FMT_COMPILE("zfs import -N -R {} {} &>>/tmp/cachyos-install.log"), mountpoint_info, zfs_zpool_name);
        if (!gucc::utils::exec_checked(zfs_import_cmd)) {
            spdlog::error("Failed to import zpool: {}", zfs_import_cmd);
            detail::msgbox_widget(fmt::format("\nFailed to import ZFS pool {}.\nCheck logs for details.\n", zfs_zpool_name));
            return false;
        }
        spdlog::info("Imported ZFS pool {}", zfs_zpool_name);
#else
        spdlog::info("[DRY-RUN] Would import ZFS pool {}", zfs_zpool_name);
#endif
        return true;
    }

    // check to see if we already have a zfs root mounted
    if (gucc::fs::utils::get_mountpoint_fs(mountpoint_info) == "zfs"sv) {
        detail::infobox_widget("\nUsing ZFS root on \'/\'\n"sv);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return true;
    }

    // 1. Get user selections
    auto selection_opt = select_root_partition_info();
    if (!selection_opt) {
        spdlog::info("Root partition selection cancelled or failed.");
        return false;
    }

    // 2. Apply the selected actions
    if (!utils::apply_root_partition_actions(*selection_opt, {}, partitions)) {
        spdlog::error("Failed to apply root partition actions for device {}", selection_opt->device);
        return false;
    }

    return true;
}

auto select_swap_info() noexcept -> std::optional<utils::SwapSelection> {
    static constexpr auto sel_swap_body = "\nSelect SWAP Partition.\nIf using a Swapfile, it will initially set the same size as your RAM.\n"sv;
    static constexpr auto sel_swap_file = "Swapfile"sv;

    auto* config_instance       = Config::instance();
    auto& config_data           = config_instance->data();
    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);

    std::string answer{};
    {
        std::vector<std::string> temp{"None -"};
        const auto& root_filesystem = gucc::fs::utils::get_mountpoint_fs(mountpoint_info);
        if (!(root_filesystem == "zfs"sv || root_filesystem == "btrfs"sv)) {
            temp.emplace_back("Swapfile -");
        }
        const auto& partitions = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);
        temp.reserve(partitions.size());
        std::ranges::copy(partitions, std::back_inserter(temp));

        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        bool success{};
        auto ok_callback = [&] {
            const auto& src   = temp[static_cast<std::size_t>(selected)];
            const auto& lines = gucc::utils::make_multiline(src, false, ' ');
            answer            = lines[0];
            success           = true;
            screen.ExitLoopClosure()();
        };
        /* clang-format off */
        detail::menu_widget(temp, ok_callback, &selected, &screen, sel_swap_body, {.text_size = size(HEIGHT, GREATER_THAN, 1)});
        if (!success) { return std::nullopt; }
        /* clang-format on */
    }

    utils::SwapSelection selection{};
    if (answer == "None"sv) {
        selection.type = utils::SwapSelection::Type::None;
        return selection;
    }

    if (answer == sel_swap_file) {
        selection.type           = utils::SwapSelection::Type::Swapfile;
        const auto& total_memory = gucc::utils::exec("grep MemTotal /proc/meminfo | awk '{print $2/1024}' | sed 's/\\..*//'");
        std::string value{fmt::format(FMT_COMPILE("{}M"), total_memory)};
        if (!detail::inputbox_widget(value, "\nM = MB, G = GB\n", size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
            return std::nullopt;
        }

        while (!gucc::utils::exec_checked(fmt::format(FMT_COMPILE("echo '{}' | grep -q 'M\\|G'"), value))) {
            detail::msgbox_widget(fmt::format(FMT_COMPILE("\n{} Error: M = MB, G = GB\n"), sel_swap_file));
            value = fmt::format(FMT_COMPILE("{}M"), total_memory);
            if (!detail::inputbox_widget(value, "\nM = MB, G = GB\n", size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
                return std::nullopt;
            }
        }
        selection.swapfile_size = std::move(value);
        return selection;
    }

    // Partition swap
    selection.type   = utils::SwapSelection::Type::Partition;
    selection.device = answer;

    // Check if partition already has swap
    const auto& swap_part  = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -o FSTYPE '{}' | grep -i 'swap'"), answer));
    selection.needs_mkswap = (swap_part != "swap"sv);

    if (selection.needs_mkswap) {
        const auto& do_swap = detail::yesno_widget(fmt::format(FMT_COMPILE("\nmkswap {}\n"), answer), size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
        /* clang-format off */
        if (!do_swap) { return std::nullopt; }
        /* clang-format on */
    }

    return selection;
}

// Gather ESP selection from user without applying any changes
auto select_esp_info() noexcept -> std::optional<utils::EspSelection> {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    const auto& sys_info  = std::get<std::string>(config_data["SYSTEM"]);

    /* clang-format off */
    if (sys_info != "UEFI"sv) { return utils::EspSelection{}; }
    /* clang-format on */

    utils::EspSelection selection{};

    // Select ESP partition
    {
        const auto& partitions_list = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);

        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        bool success{};
        auto ok_callback = [&] {
            const auto& src   = partitions_list[static_cast<std::size_t>(selected)];
            const auto& lines = gucc::utils::make_multiline(src, false, ' ');
            selection.device  = lines[0];
            success           = true;
            screen.ExitLoopClosure()();
        };

        /* clang-format off */
        static constexpr auto esp_part_body = "\nSelect BOOT partition.\n"sv;
        detail::menu_widget(partitions_list, ok_callback, &selected, &screen, esp_part_body, {.text_size = size(HEIGHT, GREATER_THAN, 1)});
        if (!success) { return std::nullopt; }
        /* clang-format on */
    }

    // Check if already fat/vfat
    const auto& is_fat_part = gucc::utils::exec_checked(fmt::format(FMT_COMPILE("fsck -N {} | grep -q fat"), selection.device));
    if (!is_fat_part) {
        const auto& content        = fmt::format(FMT_COMPILE("\nThe UEFI partition {} has already been formatted.\n \nReformat? Doing so will erase ALL data already on that partition.\n"), selection.device);
        selection.format_requested = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    }

    // Select mountpoint
    {
        const auto& luks                             = std::get<std::int32_t>(config_data["LUKS"]);
        static constexpr auto MntUefiBody            = "\nSelect UEFI Mountpoint.\n \n/boot/efi is recommended for multiboot systems.\n/boot is required for systemd-boot.\n"sv;
        static constexpr auto MntUefiCrypt           = "\nSelect UEFI Mountpoint.\n \n/boot/efi is recommended for multiboot systems and required for full disk encryption.\nEncrypted /boot is supported only by grub and can lead to slow startup.\n \n/boot is required for systemd-boot and for refind when using encryption.\n"sv;
        const auto& MntUefiMessage                   = (luks == 0) ? MntUefiBody : MntUefiCrypt;
        const std::vector<std::string> radiobox_list = {
            "/boot/efi",
            "/boot",
        };

        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        auto ok_callback = [&] {
            selection.mountpoint = radiobox_list[static_cast<std::size_t>(selected)];
            screen.ExitLoopClosure()();
        };
        detail::radiolist_widget(radiobox_list, ok_callback, &selected, &screen, {.text = MntUefiMessage}, {.text_size = nothing});
    }

    /* clang-format off */
    if (selection.mountpoint.empty()) { return std::nullopt; }
    /* clang-format on */

    return selection;
}

auto select_additional_partition_info() noexcept -> std::optional<utils::AdditionalPartSelection> {
    auto* config_instance   = Config::instance();
    auto& config_data       = config_instance->data();
    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);

    utils::AdditionalPartSelection selection{};

    // Select device
    {
        const auto& partitions_lines = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);
        std::vector<std::string> temp{"Done -"};
        temp.reserve(partitions_lines.size());
        std::ranges::copy(partitions_lines, std::back_inserter(temp));

        auto screen = ScreenInteractive::Fullscreen();
        std::int32_t selected{};
        bool success{};
        auto ok_callback = [&] {
            const auto& src   = temp[static_cast<std::size_t>(selected)];
            const auto& lines = gucc::utils::make_multiline(src, false, ' ');
            selection.device  = lines[0];
            success           = true;
            screen.ExitLoopClosure()();
        };
        /* clang-format off */
        static constexpr auto extra_part_body = "\nSelect additional partitions in any order, or 'Done' to finish.\n"sv;
        detail::menu_widget(temp, ok_callback, &selected, &screen, extra_part_body, {.text_size = size(HEIGHT, GREATER_THAN, 1)});
        if (!success) { return std::nullopt; }
        /* clang-format on */
    }

    if (selection.device == "Done"sv) {
        return utils::AdditionalPartSelection{.device = "Done", .mountpoint = "", .fstype = "", .mkfs_command = "", .mount_opts = "", .format_requested = false};
    }

    // Select filesystem
    auto fs_selection = select_fs_and_cmd();
    /* clang-format off */
    if (!fs_selection) { return std::nullopt; }
    /* clang-format on */

    selection.fstype           = fs_selection->first;
    selection.mkfs_command     = (selection.fstype == "skip"sv) ? ""s : fs_selection->second;
    selection.format_requested = !selection.mkfs_command.empty();

    // Select mountpoint
    /* clang-format off */
    // Ask user for mountpoint. Don't give /boot as an example for UEFI systems!
    std::string_view mnt_examples = "/boot\n/home\n/var"sv;
    if (system_info == "UEFI"sv) { mnt_examples = "/home\n/var"sv; }
    /* clang-format on */

    std::string value{"/"};
    static constexpr auto extra_part_body1 = "Specify partition mountpoint. Ensure\nthe name begins with a forward slash (/).\nExamples include:"sv;
    if (!detail::inputbox_widget(value, fmt::format(FMT_COMPILE("\n{}\n{}\n"), extra_part_body1, mnt_examples))) {
        return std::nullopt;
    }
    selection.mountpoint = std::move(value);

    // Validate mountpoint
    while ((selection.mountpoint.size() <= 1) || (selection.mountpoint[0] != '/') || gucc::utils::contains(selection.mountpoint, " ")) {
        detail::msgbox_widget("\nPartition cannot be mounted due to a problem with the mountpoint name.\nA name must be given after a forward slash.\n");
        selection.mountpoint = "/";
        if (!detail::inputbox_widget(selection.mountpoint, fmt::format(FMT_COMPILE("\n{}\n{}\n"), extra_part_body1, mnt_examples))) {
            return std::nullopt;
        }
    }

    // Select mount options
    if (selection.fstype != "skip"sv) {
        selection.mount_opts = select_mount_opts(selection.device, selection.fstype);
    } else {
        selection.mount_opts = select_mount_opts(selection.device, selection.fstype, true);
    }

    return selection;
}

auto gather_mount_selections() noexcept -> std::optional<utils::MountSelections> {
    auto* config_instance       = Config::instance();
    auto& config_data           = config_instance->data();
    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);

    utils::MountSelections selections{};

    // 1. Root partition
    auto root_opt = select_root_partition_info();
    if (!root_opt) {
        spdlog::info("Root partition selection cancelled.");
        return std::nullopt;
    }
    selections.root = std::move(*root_opt);

    // 2. BTRFS subvolume preferences (only relevant for btrfs root)
    if (selections.root.fstype == "btrfs"sv) {
        const auto& create_subvolumes = detail::yesno_widget("\nWould you like to create subvolumes in it? \n", size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
        if (create_subvolumes) {
            selections.btrfs_subvolumes = utils::select_btrfs_subvolumes(mountpoint_info);
        }
    }

    // 3. Swap
    auto swap_opt = select_swap_info();
    if (!swap_opt) {
        spdlog::info("Swap selection cancelled.");
        return std::nullopt;
    }
    selections.swap = std::move(*swap_opt);

    // 4. Additional partitions
    while (true) {
        auto part_opt = select_additional_partition_info();
        if (!part_opt) {
            spdlog::info("Additional partition selection cancelled.");
            return std::nullopt;
        }
        if (part_opt->device == "Done"sv) {
            break;
        }
        selections.additional.emplace_back(std::move(*part_opt));
    }

    // 5. ESP (UEFI only)
    auto esp_opt = select_esp_info();
    if (!esp_opt) {
        spdlog::info("ESP selection cancelled.");
        return std::nullopt;
    }
    selections.esp = std::move(*esp_opt);

    return selections;
}

auto preview_partition_schema(const utils::MountSelections& selections) noexcept -> bool {
    std::string preview{"\n"};
    preview += "═══ Partition Plan ═══\n\n";

    // Root
    {
        const auto& r      = selections.root;
        const auto& action = r.format_requested
            ? fmt::format(FMT_COMPILE("format as {}"), r.fstype)
            : "keep existing"s;
        preview += fmt::format(FMT_COMPILE("  Root: {} ({})\n"), r.device, action);
        if (!r.mount_opts.empty()) {
            preview += fmt::format(FMT_COMPILE("        mount opts: {}\n"), r.mount_opts);
        }
    }

    // BTRFS subvolumes
    if (!selections.btrfs_subvolumes.empty()) {
        preview += "        subvolumes:\n";
        for (const auto& sv : selections.btrfs_subvolumes) {
            preview += fmt::format(FMT_COMPILE("          {} → {}\n"), sv.subvolume, sv.mountpoint);
        }
    }

    preview += "\n";

    // ESP
    if (!selections.esp.device.empty()) {
        const auto& action = selections.esp.format_requested ? "format vfat"s : "keep existing"s;
        preview += fmt::format(FMT_COMPILE("  Boot: {} ({}) → {}\n"), selections.esp.device, action, selections.esp.mountpoint);
        preview += "\n";
    }

    // Swap
    switch (selections.swap.type) {
    case utils::SwapSelection::Type::Swapfile:
        preview += fmt::format(FMT_COMPILE("  Swap: swapfile ({})\n\n"), selections.swap.swapfile_size);
        break;
    case utils::SwapSelection::Type::Partition: {
        const auto& action = selections.swap.needs_mkswap ? "mkswap"s : "existing"s;
        preview += fmt::format(FMT_COMPILE("  Swap: {} ({})\n\n"), selections.swap.device, action);
        break;
    }
    case utils::SwapSelection::Type::None:
        preview += "  Swap: none\n\n";
        break;
    }

    // Additional
    for (const auto& p : selections.additional) {
        const auto& action = p.format_requested
            ? fmt::format(FMT_COMPILE("format as {}"), p.fstype)
            : "keep existing"s;
        preview += fmt::format(FMT_COMPILE("  {} → {} ({})\n"), p.device, p.mountpoint, action);
    }
    if (!selections.additional.empty()) {
        preview += "\n";
    }

    preview += "─────────────────────────────\n";
    preview += "Data on formatted partitions will be lost!\n\n";
    preview += "Proceed with these changes?\n";

    return detail::yesno_widget(preview, size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40));
}

void mount_partitions() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    // Warn users that they CAN mount partitions without formatting them!
    static constexpr auto content = "\nIMPORTANT: Partitions can be mounted without formatting them\nby selecting the 'Do not format' option listed at the top of\nthe file system menu.\n \nEnsure the correct choices for mounting and formatting\nare made as no warnings will be provided, with the exception of\nthe UEFI boot partition.\n"sv;
    detail::msgbox_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 70));

    // LVM Detection. If detected, activate.
    tui::lvm_detect();

    // Ensure partitions are unmounted (i.e. where mounted previously)
    config_data["INCLUDE_PART"] = "part\\|lvm\\|crypt";
    utils::umount_partitions();

    // We need to remount the zfs filesystems that have defined mountpoints already
    gucc::utils::exec("zfs mount -aO &>/dev/null");

    // Get list of available partitions
    utils::find_partitions();

    // get all mount input from user
    auto selections = gather_mount_selections();
    if (!selections) {
        spdlog::info("Mount partition selection cancelled by user.");
        return;
    }

    // ask user to review and approve selection
    if (!preview_partition_schema(*selections)) {
        spdlog::info("Partition schema declined.");
        return;
    }

    // apply changes on disk
    if (!utils::apply_mount_selections(*selections)) {
        detail::msgbox_widget("\nFailed to apply partition changes.\nCheck the log for details.\n");
    }
}

void configure_mirrorlist() noexcept {
    const std::vector<std::string> menu_entries = {
        "Edit Pacman Configuration",
        "Rank mirrors",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            screen.WithRestoredIO([]() { tui::edit_pacman_conf(); });
            break;
        case 1:
            screen.WithRestoredIO([]() { gucc::utils::exec("cachyos-rate-mirrors"sv, true); });
            break;
        default:
            screen.ExitLoopClosure()();
            break;
        }
    };
    static constexpr auto mirrorlist_body = "\nThe pacman configuration file can be edited\nto enable multilib and other repositories.\n"sv;
    auto text_size                        = size(HEIGHT, LESS_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, mirrorlist_body, {size(HEIGHT, LESS_THAN, 3), std::move(text_size)});
}

void create_partitions() noexcept {
    static constexpr auto optwipe = "Securely Wipe Device (optional)"sv;
    static constexpr auto optauto = "Automatic Partitioning"sv;

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
            screen.WithRestoredIO([&]() {
#ifdef NDEVENV
                gucc::utils::exec(fmt::format(FMT_COMPILE("{} {}"), selected_entry, std::get<std::string>(config_data["DEVICE"])), true);
#else
                spdlog::debug("to be executed: {}", fmt::format(FMT_COMPILE("{} {}"), selected_entry, std::get<std::string>(config_data["DEVICE"])));
#endif
            });
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
            screen.WithRestoredIO([]() {
                tui::chroot_interactive();
            });

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
            tui::remove_pkgs();
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
            screen.WithRestoredIO([]() {
                tui::chroot_interactive();
            });

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
        case 3:
            SPDLOG_ERROR("Implement me!");
            break;
        case 4:
            tui::lvm_menu();
            break;
        case 5:
            tui::luks_menu_advanced();
            break;
        case 6:
            tui::zfs_menu();
            break;
        case 7:
            tui::mount_partitions();
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

void init() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    const auto& menus     = std::get<std::int32_t>(config_data["menus"]);
    auto& headless_mode   = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);

    if (menus == 1) {
        tui::menu_simple();
        return;
    } else if (menus == 2) {
        headless_mode = 0;
        tui::menu_advanced();
        return;
    }

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
        headless_mode = 0;
        tui::menu_advanced();
        break;
    default:
        break;
    }
}

}  // namespace tui
