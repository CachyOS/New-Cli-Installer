#include "tui.hpp"
#include "crypto.hpp"
#include "definitions.hpp"
#include "disk.hpp"
#include "drivers.hpp"
#include "installer_data.hpp"
#include "installer_state.hpp"
#include "misc.hpp"
#include "simple_tui.hpp"
#include "utils.hpp"
#include "widgets.hpp"

// instlib
#include "cachyos/config.hpp"
#include "cachyos/disk.hpp"
#include "cachyos/packages.hpp"
#include "cachyos/types.hpp"
#include "cachyos/validation.hpp"
#include "cachyos/validators.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/bootloader.hpp"
#include "gucc/btrfs.hpp"
#include "gucc/error.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/kmod.hpp"
#include "gucc/locale.hpp"
#include "gucc/memory.hpp"
#include "gucc/mirrors.hpp"
#include "gucc/mount_partitions.hpp"
#include "gucc/mtab.hpp"
#include "gucc/partition.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/ranges_utils.hpp"
#include "gucc/repos.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/swap.hpp"
#include "gucc/system_query.hpp"
#include "gucc/timezone.hpp"
#include "gucc/zfs.hpp"
#include "gucc/zfs_query.hpp"

#include <sys/mount.h>  // for mount

#include <cstdlib>  // for exit

#include <algorithm>   // for any_of, find
#include <array>       // for array
#include <chrono>      // for seconds
#include <filesystem>  // for exists, is_directory
#include <functional>  // for function
#include <optional>    // for optional
#include <ranges>      // for ranges::*
#include <string>      // for basic_string
#include <vector>      // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#ifdef NDEVENV
#include "follow_process_log.hpp"
#endif

namespace fs = std::filesystem;

using namespace std::string_literals;
using namespace std::string_view_literals;
using namespace std::chrono_literals;

namespace {

using gucc::bootloader::BootloaderType;

// re-export
constexpr std::array uefi_bootloaders{
    BootloaderType::Grub,
    BootloaderType::Refind,
    BootloaderType::SystemdBoot,
    BootloaderType::Limine,
};

auto index_of(const std::vector<std::string>& entries, std::string_view value) noexcept -> std::size_t {
    return gucc::utils::index_of(entries, value).value_or(0);
}

auto select_partition(const std::vector<std::string>& entries, std::string_view body) noexcept -> std::optional<std::string> {
    return tui::detail::select_value(entries, body).transform(installer::data::parse_device_name);
}

auto select_uefi_bootloader(std::string_view body) noexcept -> std::optional<BootloaderType> {
    const auto entries = uefi_bootloaders
        | std::ranges::views::transform([](BootloaderType type) { return std::string{gucc::bootloader::bootloader_to_string(type)}; })
        | std::ranges::to<std::vector<std::string>>();
    return tui::detail::select_from_menu(entries, body).transform([](std::size_t idx) { return uefi_bootloaders.at(idx); });
}

template <auto Field, typename T>
void apply_setting(T&& value, [[maybe_unused]] std::string_view what) noexcept {
    cachyos::installer::SystemSettings settings{};
    settings.*Field = std::forward<T>(value);
#ifdef NDEVENV
    if (auto res = cachyos::installer::apply_system_settings(settings, utils::get_mountpoint()); !res) {
        spdlog::error("Failed to set {}: {}", what, res.error());
    }
#endif
}

// Menu action that leaves the menu.
template <typename Ready, typename Action>
auto guarded(Ready ready, Action action) noexcept -> std::function<bool()> {
    return [ready, action] {
        if (!ready()) {
            return false;
        }
        action();
        return true;
    };
}

auto base_installed_marker() noexcept -> std::string {
    return gucc::utils::target_path(utils::get_mountpoint(), "/.base_installed"sv);
}

auto zfs_child_datasets() noexcept -> std::vector<std::string> {
    return gucc::fs::zfs_query::detail::child_dataset_names(gucc::fs::list_zfs_datasets());
}

void announce_lvm_activation() noexcept {
    tui::detail::infobox_for("Existing Logical Volume Management (LVM) detected. Activating. Please Wait..."sv, 2s);
}

}  // namespace

namespace tui {

bool exit_done() noexcept {
#ifdef NDEVENV
    static constexpr auto close_inst_body = "\nClose installer?\n"sv;
    const auto mountpoint                 = utils::get_mountpoint();
    const auto& target_mounts             = gucc::mtab::parse_mtab(mountpoint);
    if (target_mounts && !target_mounts->empty()) {
        const auto check = cachyos::installer::final_check(utils::build_install_context());
        std::string checklist;
        for (const auto& item : std::ranges::views::concat(check.errors, check.warnings)) {
            checklist += fmt::format(FMT_COMPILE("- {}\n"), item);
        }
        const auto& do_close = detail::yesno_widget(fmt::format(FMT_COMPILE("{}{}\n"), close_inst_body, checklist));
        /* clang-format off */
        if (!do_close) { return false; }
        /* clang-format on */

        spdlog::info("exit installer.");

        static constexpr auto LogInfo = "Would you like to save\nthe installation-log\nto the installed system? It will be copied to"sv;
        const auto& do_save_log       = detail::yesno_widget(fmt::format(FMT_COMPILE("{} {}/cachyos-install.log"), LogInfo, mountpoint));
        if (do_save_log) {
            fs::copy_file("/tmp/cachyos-install.log", gucc::utils::target_path(mountpoint, "/cachyos-install.log"sv), fs::copy_options::overwrite_existing);
        }
        utils::umount_partitions();
        utils::clear_screen();
        return true;
    }
    const auto& do_close = detail::yesno_widget(close_inst_body);
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
    static constexpr auto fstab_body = "The FSTAB file (File System TABle) sets what storage devices and partitions are to be mounted, and how they are to be used.\n\nUUID (Universally Unique IDentifier) used."sv;
    /* clang-format off */
    if (!detail::yesno_widget(fstab_body)) { return; }
    /* clang-format on */

#ifdef NDEVENV
    if (auto res = cachyos::installer::generate_fstab(utils::get_mountpoint()); !res) {
        spdlog::error("Failed to generate fstab: {}", res.error());
    }
#else
    spdlog::info("Generating fstab on {}", utils::get_mountpoint());
#endif
}

// Set system hostname
void set_hostname() noexcept {
    std::string hostname{"cachyos"};
    static constexpr auto hostname_body = "The hostname is used to identify the system on a network.\n\nIt is restricted to alphanumeric characters, can contain a hyphen (-) - but not at the start or end - and must be no longer than 63 characters."sv;
    if (!detail::inputbox_widget(hostname, hostname_body)) {
        return;
    }
    hostname = std::string{gucc::utils::trim(hostname)};
    if (auto valid = cachyos::installer::validate_hostname(hostname); !valid) {
        detail::msgbox_widget(fmt::format(FMT_COMPILE("Invalid hostname: {}"), valid.error()));
        return;
    }

    spdlog::info("Setting hostname {}", hostname);
    apply_setting<&cachyos::installer::SystemSettings::hostname>(hostname, "hostname"sv);
}

// Set system language
void set_locale() noexcept {
    const auto& locales = gucc::locale::get_possible_locales();

    static constexpr auto langBody = "Choose the system language.\n\nThe format is language_COUNTRY (e.g. en_US is english, United States; en_GB is english, Great Britain)."sv;
    const auto locale              = detail::select_value(locales, langBody, index_of(locales, "en_US.UTF-8"sv));
    /* clang-format off */
    if (!locale) { return; }
    /* clang-format on */

    spdlog::info("Selected locale: {}", *locale);
    apply_setting<&cachyos::installer::SystemSettings::locale>(*locale, "locale"sv);
}

// Set keymap for X11
void set_xkbmap() noexcept {
    const auto& xkbmap_list = gucc::locale::get_x11_keymap_layouts();
    if (xkbmap_list.empty()) {
        detail::msgbox_widget("Unable to get X11 keymap layouts."sv);
        return;
    }

    static constexpr auto xkbmap_body = "Select Desktop Environment Keymap."sv;
    const auto xkbmap                 = detail::select_value(xkbmap_list, xkbmap_body, index_of(xkbmap_list, "us"sv));
    /* clang-format off */
    if (!xkbmap) { return; }
    /* clang-format on */

    spdlog::info("Selected xkbmap: {}", *xkbmap);
    apply_setting<&cachyos::installer::SystemSettings::xkbmap>(*xkbmap, "xkbmap"sv);
}

auto select_vconsole_keymap(std::string_view body_text) noexcept -> std::string {
    const auto& keymaps = gucc::locale::get_vconsole_keymap_list();
    if (keymaps.empty()) {
        detail::msgbox_widget("Unable to get console keymap list."sv);
        return {};
    }

    const auto& current_keymap = tui::state().inputs.ctx.keymap;
    const auto initial         = index_of(keymaps, current_keymap.empty() ? "us"sv : std::string_view{current_keymap});
    return detail::select_value(keymaps, body_text, initial).value_or(std::string{});
}

void select_keymap() noexcept {
    auto& current_keymap = tui::state().inputs.ctx.keymap;

    // does user want to change the default settings?
    static constexpr auto default_keymap_msg = "Currently configured keymap setting is:"sv;
    const auto& content                      = fmt::format(FMT_COMPILE(" {}\n\n[ {} ]"), default_keymap_msg, current_keymap);
    /* clang-format off */
    if (!detail::yesno_widget(content)) { return; }
    /* clang-format on */

    static constexpr auto vc_keymap_body = "Select the keymap for the current session. This sets the keyboard layout for typing in the current session."sv;
    auto keymap_choice                   = select_vconsole_keymap(vc_keymap_body);
    /* clang-format off */
    if (keymap_choice.empty()) { return; }
    /* clang-format on */

    spdlog::info("Selected keymap: {}", keymap_choice);
    current_keymap = keymap_choice;
    apply_setting<&cachyos::installer::SystemSettings::keymap>(std::move(keymap_choice), "keymap"sv);
}

// Set Zone and Sub-Zone
bool set_timezone() noexcept {
    const auto& zone_list               = gucc::timezone::get_timezone_regions();
    static constexpr auto timezone_body = "The time zone is used to correctly set your system clock."sv;
    const auto zone                     = detail::select_value(zone_list, timezone_body);
    /* clang-format off */
    if (!zone) { return false; }
    /* clang-format on */

    const auto& city_list                   = gucc::timezone::get_timezone_zones(*zone);
    static constexpr auto sub_timezone_body = "Select the city nearest to you."sv;
    const auto city                         = detail::select_value(city_list, sub_timezone_body);
    /* clang-format off */
    if (!city) { return false; }
    /* clang-format on */

    const auto& timezone = fmt::format(FMT_COMPILE("{}/{}"), *zone, *city);
    const auto& content  = fmt::format(FMT_COMPILE("Set Time Zone: {}?"), timezone);
    /* clang-format off */
    if (!detail::yesno_widget(content)) { return false; }
    /* clang-format on */

    spdlog::info("Timezone is set to {}", timezone);
    apply_setting<&cachyos::installer::SystemSettings::timezone>(timezone, "timezone"sv);
    return true;
}

// Set system clock type
void set_hw_clock() noexcept {
    using HwClock = cachyos::installer::SystemSettings::HwClock;
    static constexpr std::array clock_types{HwClock::UTC, HwClock::Localtime};
    const std::vector<std::string> menu_entries{"utc", "localtime"};

    static constexpr auto hw_clock_body = "UTC is the universal time standard, and is recommended unless dual-booting with Windows."sv;
    const auto idx                      = detail::select_from_menu(menu_entries, hw_clock_body);
    /* clang-format off */
    if (!idx) { return; }
    /* clang-format on */

    spdlog::info("Clock type is: {}", menu_entries.at(*idx));
    apply_setting<&cachyos::installer::SystemSettings::hw_clock>(clock_types.at(*idx), "hw clock"sv);
}

// Set password for root user
void set_root_password() noexcept {
    const auto pass = detail::password_prompt("Enter Root password"sv, "Re-enter Root password"sv);
    if (!pass) {
        return;
    }
#ifdef NDEVENV
    if (auto res = cachyos::installer::set_root_password(*pass, utils::get_mountpoint()); !res) {
        spdlog::error("Failed to set root password: {}", res.error());
    }
#endif
}

// Create user on the system
void create_new_user() noexcept {
    static constexpr auto user_body     = "Enter the user name. Letters MUST be lower case."sv;
    static constexpr auto user_err_body = "An incorrect user name was entered. Please try again. Use up to 32 of a-z, 0-9, '_' and '-', starting with a letter or '_'."sv;
    const auto user                     = detail::prompt_validated(user_body, user_err_body, cachyos::installer::is_valid_username);
    /* clang-format off */
    if (!user) { return; }
    /* clang-format on */

    std::string_view shell{};
    {
        const std::vector<std::string> radiobox_list = {
            "zsh",
            "bash",
            "fish",
        };
        const auto picked = detail::select_radio(radiobox_list, "Choose the default shell."sv);
        if (picked == "zsh"sv) {
            shell = "/usr/bin/zsh"sv;
        } else if (picked == "bash"sv) {
            shell = "/bin/bash"sv;
        } else if (picked == "fish"sv) {
            shell = "/usr/bin/fish"sv;
        }
    }

    const auto pass = detail::password_prompt(fmt::format(FMT_COMPILE("Enter password for {}"), *user), "Re-enter the password."sv);
    if (!pass) {
        return;
    }

    detail::infobox_for("Creating User and setting groups..."sv, 2s);
    utils::create_new_user(*user, *pass, shell);
}

// Install pkgs from user input
void install_cust_pkgs() noexcept {
    std::string packages{};
    static constexpr auto content = "Type any extra packages you would like to add, separated by spaces.\n\nFor example, to install Firefox, MPV, FZF: firefox mpv fzf"sv;
    if (!detail::inputbox_widget(packages, content)) {
        return;
    }
    packages = std::string{gucc::utils::trim(packages)};
    // If at least one package, install.
    /* clang-format off */
    if (packages.empty()) { return; }
    /* clang-format on */

    utils::install_from_pkglist(packages);
}

void remove_pkgs() noexcept {
    std::string packages{};
    static constexpr auto content = "Type any packages you would like to remove, separated by spaces.\n\nFor example, to remove Firefox, MPV, FZF: firefox mpv fzf"sv;
    if (!detail::inputbox_widget(packages, content)) {
        return;
    }
    packages = std::string{gucc::utils::trim(packages)};
    utils::remove_pkgs(packages);
}

void chroot_interactive() noexcept {
    static constexpr auto chroot_return = "You will now chroot into your installed system. You can do changes almost as if you had booted into your installation.\n\nType \"exit\" to exit chroot."sv;
    detail::infobox_for(chroot_return, 1s);

#ifdef NDEVENV
    detail::run_in_terminal({"/usr/bin/arch-chroot"s, std::string{utils::get_mountpoint()}, "/usr/bin/bash"s});
#else
    detail::run_in_terminal({"/usr/bin/bash"s});
#endif
}

void uefi_bootloader() noexcept {
#ifdef NDEVENV
    // Ensure again that efivarfs is mounted
    static constexpr auto efi_path = "/sys/firmware/efi/"sv;
    if (fs::exists(efi_path) && fs::is_directory(efi_path)) {
        // Mount efivarfs if it is not already mounted
        static constexpr auto efivars_path = "/sys/firmware/efi/efivars";
        if (gucc::fs::utils::get_mountpoint_source(efivars_path).empty() && (mount("efivarfs", efivars_path, "efivarfs", 0, "") != 0)) {
            perror("utils::uefi_bootloader");
            exit(1);
        }
    }
#endif

    static constexpr auto bootloaderInfo = "Refind can be used standalone or in conjunction with other bootloaders as a graphical bootmenu. It autodetects all bootable systems at boot time. Grub supports encrypted /boot partition and detects all bootable systems when you update your kernels. It supports booting .iso files from a harddrive and automatic boot entries for btrfs snapshots. Systemd-boot is very light and simple and has little automation. It autodetects windows, but is otherwise unsuited for multibooting. Limine is lightweight bootloader with Btrfs snapshots loading support."sv;
    const auto bootloader                = select_uefi_bootloader(bootloaderInfo);
    /* clang-format off */
    if (!bootloader) { return; }
    /* clang-format on */

    tui::state().bootloader = *bootloader;
    utils::install_bootloader(*bootloader);
}

void install_base() noexcept {
    const auto base_installed = base_installed_marker();
    if (fs::exists(base_installed)) {
        static constexpr auto content = "A CachyOS Base has already been installed on this partition. Proceed anyway?"sv;
        /* clang-format off */
        if (!detail::yesno_widget(content)) { return; }
        /* clang-format on */
        std::error_code err{};
        fs::remove(base_installed, err);
    }
    const auto mountpoint = utils::get_mountpoint();

    // reuse the session keymap virt console rather than asking twice.
    spdlog::info("Using console keymap for target: {}", tui::state().inputs.ctx.keymap);

    const auto available_kernels = installer::data::to_vec(installer::data::available_kernels);

    // no kernel picked is fine only when the target already has one.
    const auto has_kernel = [&](const std::vector<std::string>& picked) {
        if (std::ranges::any_of(picked, [](const std::string& pkg) { return pkg.contains("linux"sv); })) {
            return true;
        }
        const auto& installed_kernels = utils::list_installed_kernels(gucc::utils::target_path(mountpoint, "/boot"sv));
        if (installed_kernels.empty()) {
            detail::msgbox_widget("At least one kernel must be selected."sv);
            return false;
        }
        detail::msgbox_widget(fmt::format(FMT_COMPILE("{} detected"), fmt::join(installed_kernels, "\n")));
        return true;
    };

    static constexpr auto InstStandBseBody = "The base package group will be installed automatically. The base-devel package group is required to use the Arch User Repository (AUR)."sv;
    static constexpr auto base_title       = "New CLI Installer | Install Base"sv;
    const auto picked                      = detail::select_many(available_kernels, InstStandBseBody, base_title, has_kernel);
    /* clang-format off */
    if (!picked || picked->empty()) { return; }
    /* clang-format on */

    const auto packages = gucc::utils::join(*picked, ' ');
    spdlog::info("selected: {}", packages);

    tui::state().inputs.ctx.kernel = packages;
    utils::install_base(packages);
}

void install_desktop() noexcept {
#ifdef NDEVENV
    if (!fs::exists(base_installed_marker())) {
        detail::infobox_for("A CachyOS Base is not installed on this partition."sv, 1s);
        return;
    }
#endif

    // TODO(vnepogodin): make available DEs configurable or fetchable from net-profiles
    const auto available_des = installer::data::to_vec(installer::data::available_desktops);

    static constexpr auto desktop_title = "New CLI Installer | Install Desktop"sv;
    const auto desktop_env              = detail::select_radio(available_des, "Please choose a desktop environment."sv, desktop_title);
    /* clang-format off */
    if (!desktop_env || desktop_env->empty()) { return; }
    /* clang-format on */

    spdlog::info("selected: {}", *desktop_env);
    utils::install_desktop(*desktop_env);
}

// Base Configuration
void config_base_menu() noexcept {
    static constexpr auto config_base_body = "Basic configuration of the base."sv;
    detail::menu_loop(config_base_body, {
                                            {"Generate FSTAB", [] { tui::generate_fstab(); return true; }},
                                            {"Set Hostname", [] { tui::set_hostname(); return true; }},
                                            {"Set System Locale", [] { tui::set_locale(); return true; }},
                                            {"Set Desktop Keyboard Layout", [] { tui::set_xkbmap(); return true; }},
                                            {"Set Timezone and Clock", [] {
                                                 // Cancelling the timezone leaves the menu.
                                                 if (!tui::set_timezone()) {
                                                     return false;
                                                 }
                                                 tui::set_hw_clock();
                                                 return true;
                                             }},
                                            {"Set Root Password", [] { tui::set_root_password(); return true; }},
                                            {"Add New User(s)", [] { tui::create_new_user(); return true; }},
                                            {"Back", detail::kLeaveMenu},
                                        });
}

// Grub auto-detects installed kernel
void bios_bootloader() {
    static constexpr auto bootloaderInfo        = "The installation device for GRUB can be selected in the next step."sv;
    const std::vector<std::string> menu_entries = {
        std::string{gucc::bootloader::bootloader_to_string(BootloaderType::Grub)},
    };

    /* clang-format off */
    if (!detail::select_from_menu(menu_entries, bootloaderInfo)) { return; }
    if (!tui::select_device()) { return; }
    /* clang-format on */

    tui::state().bootloader = BootloaderType::Grub;
    utils::install_bootloader(BootloaderType::Grub);
}

void enable_autologin() {
    const auto target_root = utils::get_mountpoint();
    const auto& dm         = utils::enabled_display_manager(target_root);
    const auto& content    = fmt::format(FMT_COMPILE("This option enables autologin using {}.\n\nProceed?"), dm);
    /* clang-format off */
    if (!detail::yesno_widget(content)) { return; }
    /* clang-format on */

    // autologin is only offered for a single-user system
    const auto& users = utils::list_home_users(target_root);
    /* clang-format off */
    if (users.size() != 1) { return; }
    /* clang-format on */

    utils::enable_autologin(dm, users.front());
}

void tweaks_menu() noexcept {
    static constexpr auto tweaks_body = "Various configuration options"sv;
    detail::menu_loop(tweaks_body, {
                                       {"Enable Automatic Login", [] { tui::enable_autologin(); return true; }},
                                       {"Back", detail::kLeaveMenu},
                                   });
}

void select_bootloader_type() noexcept {
    if (!tui::state().inputs.ctx.is_uefi()) {
        tui::state().bootloader = BootloaderType::Grub;
        spdlog::info("BIOS mode: bootloader automatically set to GRUB");
        return;
    }

    static constexpr auto bootloaderInfo = "Select the bootloader for the system. Refind can be used standalone or in conjunction with other bootloaders. Grub supports encrypted /boot partition and detects all bootable systems. Systemd-boot is very light and simple. Limine is lightweight with Btrfs snapshots support."sv;
    const auto bootloader                = select_uefi_bootloader(bootloaderInfo);
    /* clang-format off */
    if (!bootloader) { return; }
    /* clang-format on */

    tui::state().bootloader = *bootloader;
    spdlog::info("Bootloader selected: {}", gucc::bootloader::bootloader_to_string(*bootloader));
}

void install_bootloader() noexcept {
    /* clang-format off */
    if (!utils::check_base()) { return; }
    /* clang-format on */

    if (!tui::state().inputs.ctx.is_uefi()) {
        tui::bios_bootloader();
    } else {
        tui::uefi_bootloader();
    }
}

// BIOS and UEFI
void auto_partition() noexcept {
    auto& ctx              = tui::state().inputs.ctx;
    const auto& bootloader = tui::state().bootloader;

    // Bootloader must be selected before partitioning.
    if (!bootloader) {
        detail::msgbox_widget("Please select a bootloader first (Prepare Installation -> Select Bootloader).");
        return;
    }
    spdlog::info("Using bootloader for partitioning: {}", gucc::bootloader::bootloader_to_string(*bootloader));

    // Generate and apply partition schema
    auto partitions = utils::auto_partition();
    if (partitions.empty()) {
        detail::msgbox_widget("Automatic partitioning failed. Check the log for details.");
        return;
    }

    spdlog::info("Auto-partition schema stored with {} entries", partitions.size());
    ctx.partition_schema = std::move(partitions);

    detail::msgbox_widget(gucc::disk::format_partition_table(ctx.device));
}

// Simple code to show devices / partitions.
void show_devices() noexcept {
    const auto lsblk = utils::device_listing("NAME,MODEL,TYPE,FSTYPE,SIZE,MOUNTPOINT"sv, "type=='disk' || type=='part' || type=='lvm' || type=='crypt'"sv);
    detail::msgbox_widget(lsblk);
}

// Refresh pacman keys
void refresh_pacman_keys() noexcept {
#ifdef NDEVENV
    detail::follow_task([] {
        auto res = gucc::repos::reset_target_keyring(utils::get_mountpoint());
        if (!res) {
            spdlog::error("{}", gucc::to_string(res.error()));
        }
        return res.has_value();
    });
#else
    SPDLOG_DEBUG("({}) Function is doing nothing in dev environment!", __PRETTY_FUNCTION__);
#endif
}

// This function does not assume that the formatted device is the Root installation device as
// more than one device may be formatted. Root is set in the mount_partitions function.
bool select_device() noexcept {
    auto device = detail::select_value(installer::data::get_device_list()).transform(installer::data::parse_device_name);
    /* clang-format off */
    if (!device) { return false; }
    /* clang-format on */

    tui::state().inputs.ctx.device = std::move(*device);
    return true;
}

auto select_fs_and_cmd() noexcept -> std::optional<std::pair<std::string, std::string>> {
    // TODO(vnepogodin): shouldn't be hardcoded here. keep in sync with the library
    static constexpr std::array supported_fs_types{
        gucc::fs::FilesystemType::Btrfs,
        gucc::fs::FilesystemType::Ext4,
        gucc::fs::FilesystemType::F2fs,
        gucc::fs::FilesystemType::Xfs,
    };

    std::vector<std::string> menu_entries{"Do not format -"};
    menu_entries.append_range(supported_fs_types | std::ranges::views::transform([](gucc::fs::FilesystemType fs_type) {
        return fmt::format(FMT_COMPILE("{} {}"), gucc::fs::filesystem_type_to_string(fs_type), gucc::fs::get_mkfs_command(fs_type));
    }));

    const auto idx = detail::select_from_menu(menu_entries);
    /* clang-format off */
    if (!idx) { return std::nullopt; }
    if (*idx == 0) { return std::pair{"skip"s, ""s}; }
    /* clang-format on */

    const auto fs_type = supported_fs_types.at(*idx - 1);
    return std::pair{std::string{gucc::fs::filesystem_type_to_string(fs_type)}, std::string{gucc::fs::get_mkfs_command(fs_type)}};
}

// Set static list of filesystems rather than on-the-fly. Partially as most require additional flags, and
// partially because some don't seem to be viable.
bool select_filesystem() noexcept {
    const auto& disk = tui::state().disk;

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
    const auto& file_sys  = disk.mkfs_command;
    const auto& partition = disk.partition;
    const auto& content   = fmt::format(FMT_COMPILE("Mount {}\n\n! Data on {} will be lost !"), file_sys, partition);
    if (detail::yesno_widget(content)) {
#ifdef NDEVENV
        if (auto res = gucc::fs::format_partition(partition, gucc::fs::string_to_filesystem_type(fs_selection->first)); !res) {
            spdlog::error("Failed to format {} with '{}': {}", partition, file_sys, gucc::to_string(res.error()));
            detail::msgbox_widget(fmt::format(FMT_COMPILE("Failed to format {}"), partition));
            return false;
        }
#endif
        spdlog::info("mount.{} {}", partition, file_sys);
    }

    return true;
}

auto select_mount_opts(std::string_view partition, std::string_view fstype, bool force = false) noexcept -> std::string {
    auto mount_opts_info = utils::default_mount_opts(partition, fstype);
    if (force) {
        return mount_opts_info;
    }

    // Show an input box with the default mount opts pre-filled
    static constexpr auto mount_options_body = "Edit mount options for the partition. The defaults are pre-filled based on filesystem and device type."sv;
    if (!detail::inputbox_widget(mount_opts_info, mount_options_body)) {
        return ""s;
    }
    return std::string{gucc::utils::trim(mount_opts_info)};
}

void lvm_detect() noexcept {
    utils::lvm_detect(announce_lvm_activation);
}

void lvm_del_vg() noexcept {
    const auto& vg_list = utils::lvm_show_vg();
    if (vg_list.empty()) {
        detail::msgbox_widget("No Volume Groups found.");
        return;
    }

    const auto display_list = vg_list
        | std::ranges::views::transform([](const auto& vg) { return fmt::format(FMT_COMPILE("{} ({})"), vg.first, vg.second); })
        | std::ranges::to<std::vector<std::string>>();

    static constexpr auto del_lvmvg_body = "Select Volume Group to delete. All Logical Volumes within will also be deleted."sv;
    const auto idx                       = detail::select_from_menu(display_list, del_lvmvg_body);
    /* clang-format off */
    if (!idx) { return; }
    if (!detail::yesno_widget("Confirm deletion of Volume Group(s) and Logical Volume(s)."sv)) { return; }
    /* clang-format on */

#ifdef NDEVENV
    const auto& vg_name = vg_list.at(*idx).first;
    if (!gucc::utils::exec_checked({"/usr/bin/vgremove"s, "-f"s, vg_name})) {
        detail::msgbox_widget(fmt::format(FMT_COMPILE("Failed to delete Volume Group {}."), vg_name));
    }
#endif
}

void lvm_menu() noexcept {
    tui::lvm_detect();

    // TODO(vnepogodin): VG/LV creation and full LVM wipe are not implemented yet.
    static constexpr auto not_implemented = [] { return true; };
    static constexpr auto lvm_menu_body   = "Logical Volume Management (LVM) allows 'virtual' hard drives (Volume Groups) and partitions (Logical Volumes) to be created from existing drives and partitions. A Volume Group must be created first, then one or more Logical Volumes in it.\n\nLVM can also be used with an encrypted partition to create multiple logical volumes (e.g. root and home) in it."sv;
    detail::menu_loop(lvm_menu_body, {
                                         {"Create VG and LV(s)", not_implemented},
                                         {"Delete Volume Groups", [] { tui::lvm_del_vg(); return true; }},
                                         {"Delete *ALL* VGs, LVs, PVs", not_implemented},
                                         {"Back", detail::kLeaveMenu},
                                     });
}

// creates a new zpool on an existing partition
bool zfs_create_zpool(bool do_create_zpool = true) noexcept {
    // LVM Detection. If detected, activate.
    tui::lvm_detect();

    utils::umount_partitions();
    utils::find_partitions();

    static constexpr auto zfs_zpool_partmenu_body = "Select a partition to hold the ZFS zpool"sv;
    auto partition                                = select_partition(tui::state().disk.partitions, zfs_zpool_partmenu_body);
    /* clang-format off */
    if (!partition) { return false; }
    /* clang-format on */
    tui::state().disk.partition = *partition;

    static constexpr auto zfs_zpool_body       = "Enter the name for the new zpool"sv;
    static constexpr auto zfs_zpool_validation = "zpool names must start with a letter and are limited to only alphanumeric characters and the special characters : . - _\n\nzpool names cannot start with the reserved words (log, mirror, raidz, draid or spare) nor look like a disk name (c0...)"sv;
    const auto zfs_zpool_name                  = detail::prompt_validated(zfs_zpool_body, zfs_zpool_validation, cachyos::installer::is_valid_zpool_name, "zpcachyos"s);
    /* clang-format off */
    if (!zfs_zpool_name) { return false; }
    /* clang-format on */

    tui::state().disk.zfs_zpool_name = *zfs_zpool_name;

    /* clang-format off */
    if (!do_create_zpool) { return true; }
    /* clang-format on */

    return utils::zfs_create_zpool(*partition, *zfs_zpool_name);
}

bool zfs_import_pool() noexcept {
    const auto& zlist = gucc::fs::zpool_list_importable();
    if (zlist.empty()) {
        detail::infobox_for("No pools available"sv, 3s);
        return false;
    }

    static constexpr auto zfs_menu_body = "Select a zpool from the list"sv;
    const auto zfs_zpool_name           = detail::select_value(zlist, zfs_menu_body);
    /* clang-format off */
    if (!zfs_zpool_name) { return false; }
    /* clang-format on */

#ifdef NDEVENV
    if (auto res = gucc::fs::zpool_import(*zfs_zpool_name, utils::get_mountpoint()); !res) {
        spdlog::error("{}", gucc::to_string(res.error()));
        detail::msgbox_widget(fmt::format(FMT_COMPILE("Failed to import ZFS pool {}. Check logs for details."), *zfs_zpool_name));
        return false;
    }
#endif

    return true;
}

bool zfs_new_ds(std::string_view zmount = "") noexcept {
    const auto zlist = gucc::fs::list_zfs_pools()
        | std::ranges::views::transform(&gucc::fs::ZfsPoolInfo::name)
        | std::ranges::to<std::vector<std::string>>();
    if (zlist.empty()) {
        detail::infobox_for("No pools available"sv, 3s);
        return false;
    }

    static constexpr auto zfs_menu_body = "Select a zpool from the list"sv;
    const auto zpool                    = detail::select_value(zlist, zfs_menu_body);
    /* clang-format off */
    if (!zpool) { return false; }
    /* clang-format on */

    static constexpr auto zfs_dataset_body       = "Enter a name and relative path for the dataset.\n\nFor example, if you want the dataset to be placed at zpool/data/zname, enter 'data/zname'"sv;
    static constexpr auto zfs_dataset_validation = "Dataset path components are separated by '/' and limited to alphanumeric characters and the special characters : . - _"sv;
    const auto zfs_dataset_name                  = detail::prompt_validated(zfs_dataset_body, zfs_dataset_validation, cachyos::installer::is_valid_zfs_dataset_name);
    /* clang-format off */
    if (!zfs_dataset_name) { return false; }
    /* clang-format on */

    const auto& zfs_zpath = fmt::format(FMT_COMPILE("{}/{}"), *zpool, *zfs_dataset_name);
    if (zmount == "legacy"sv) {
        if (!gucc::fs::zfs_create_dataset(zfs_zpath, zmount)) {
            spdlog::error("Failed to create zfs dataset {} at mountpoint {}", zfs_zpath, zmount);
        }
    } else if (zmount == "zvol"sv) {
        static constexpr auto zvol_size_menu_body       = "Enter the size of the zvol in megabytes(MB)"sv;
        static constexpr auto zvol_size_menu_validation = "You must enter a number greater than 0"sv;
        const auto zvol_size                            = detail::prompt_validated(zvol_size_menu_body, zvol_size_menu_validation, cachyos::installer::is_valid_zvol_size_mib);
        /* clang-format off */
        if (!zvol_size) { return false; }
        /* clang-format on */
        if (!gucc::fs::zfs_create_zvol(*zvol_size, zfs_zpath)) {
            spdlog::error("Failed to create zfs zvol {} with size {}", zfs_zpath, *zvol_size);
        }
    } else {
        spdlog::error("HELLO! IMPLEMENT ME!");
        return false;
    }

    return true;
}

void zfs_set_property() noexcept {
    const auto zlist = zfs_child_datasets();
    if (zlist.empty()) {
        detail::infobox_for("No datasets available"sv, 3s);
        return;
    }

    static constexpr auto zfs_menu_body = "Select the dataset you would like to set a property on"sv;
    const auto zdataset                 = detail::select_value(zlist, zfs_menu_body);
    /* clang-format off */
    if (!zdataset) { return; }
    /* clang-format on */

    static constexpr auto zfs_mountpoint_body  = "Enter the property and value you would like to set using the format property=mountpoint\n\nFor example, you could enter: compression=lz4 or acltype=posixacl"sv;
    static constexpr auto zfs_property_invalid = "Input must be the format property=mountpoint"sv;
    const auto zfs_property_ent                = detail::prompt_validated(zfs_mountpoint_body, zfs_property_invalid, cachyos::installer::is_valid_zfs_property);
    /* clang-format off */
    if (!zfs_property_ent) { return; }
    /* clang-format on */

    if (!gucc::fs::zfs_set_property(*zfs_property_ent, *zdataset)) {
        spdlog::error("Failed to set zfs property '{}' on dataset '{}'", *zfs_property_ent, *zdataset);
    }
}

void zfs_destroy_dataset() noexcept {
    const auto zlist = zfs_child_datasets();
    if (zlist.empty()) {
        detail::infobox_for("No datasets available"sv, 3s);
        return;
    }

    static constexpr auto zfs_destroy_menu_body = "Select the dataset you would like to permanently delete. Please note that this will recursively delete any child datasets with warning"sv;
    const auto chosen_dataset                   = detail::select_value(zlist, zfs_destroy_menu_body);
    /* clang-format off */
    if (!chosen_dataset) { return; }
    /* clang-format on */

    const auto& zdataset   = *chosen_dataset;
    const auto& content    = fmt::format(FMT_COMPILE("Please confirm that you want to irrevocably delete all the data on '{}' and the data contained on all of it's children"), zdataset);
    const auto& do_destroy = detail::yesno_widget(content);
    /* clang-format off */
    if (!do_destroy) { return; }
    /* clang-format on */

    if (auto res = gucc::fs::zfs_destroy_dataset(zdataset); !res) {
        spdlog::error("Failed to destroy zfs dataset {}: {}", zdataset, gucc::to_string(res.error()));
        detail::msgbox_widget(fmt::format(FMT_COMPILE("Failed to destroy {}"), zdataset));
    }
}

// Automated configuration of zfs. Creates a new zpool and a default set of filesystems
void zfs_auto() noexcept {
    // it creates using our preset, so don't ask for zpool name
    if (!utils::zfs_auto_pres(tui::state().disk.partition, tui::state().disk.zfs_zpool_name)) {
        detail::infobox_for("Operation failed"sv, 3s);
        return;
    }

    detail::infobox_for("Automatic zfs provisioning has been completed"sv, 3s);
}

void zfs_menu_manual() noexcept {
    static constexpr auto zfs_menu_manual_body = "Please select an option below"sv;
    detail::menu_loop(zfs_menu_manual_body, {
                                                {"Create a new zpool", [] { tui::zfs_create_zpool(); return true; }},
                                                {"Import an existing zpool", [] { tui::zfs_import_pool(); return true; }},
                                                {"Create and mount a ZFS filesystem", [] { tui::zfs_new_ds(); return true; }},
                                                {"Create a legacy ZFS filesystem", [] { tui::zfs_new_ds("legacy"sv); return true; }},
                                                {"Create a new ZVOL", [] { tui::zfs_new_ds("zvol"sv); return true; }},
                                                {"Set a property on a zfs filesystem", [] { tui::zfs_set_property(); return true; }},
                                                {"Destroy a ZFS dataset", [] { tui::zfs_destroy_dataset(); return true; }},
                                                {"Back", detail::kLeaveMenu},
                                            });
}

// The main ZFS menu
void zfs_menu() noexcept {
#ifdef NDEVENV
    // check for zfs support
    if (!gucc::kmod::load({"zfs"sv})) {
        detail::infobox_for("The kernel modules to support ZFS could not be found"sv, 3s);
        return;
    }
#endif

    static constexpr auto zfs_menu_body = "ZFS is a flexible and resilient file system that combines elements of logical volume management, RAID and traditional file systems. ZFS on Linux requires special handling and is not ideal for beginners.\n\nSelect automatic to select a partition and allow the system to automate the creation a new a zpool and datasets mounted to '/', '/home' and '/var/cache/pacman'. Manual configuration is available but requires specific knowledge of zfs."sv;
    detail::menu_loop(zfs_menu_body, {
                                         {"Automatically configure", [] { tui::zfs_auto(); return true; }},
                                         {"Manual configuration", [] { tui::zfs_menu_manual(); return true; }},
                                         {"Back", detail::kLeaveMenu},
                                     });
}

// Function to gather user choices for the root partition
auto select_root_partition_info() noexcept -> std::optional<utils::RootPartitionSelection> {
    auto& disk = tui::state().disk;

    utils::RootPartitionSelection selection{};

    // 1. Identify root partition device
    static constexpr auto sel_root_body = "Select ROOT Partition. This is where CachyOS will be installed."sv;
    auto device                         = select_partition(disk.partitions, sel_root_body);
    /* clang-format off */
    if (!device) { return std::nullopt; }
    /* clang-format on */
    selection.device = std::move(*device);
    spdlog::info("Selected root partition device: {}", selection.device);

    // NOTE: keep this for backward compatibility?
    disk.partition = selection.device;
    // a second pass through this menu must not inherit the old mountpoint
    disk.mount.clear();

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

auto select_swap_info() noexcept -> std::optional<utils::SwapSelection> {
    static constexpr auto sel_swap_body = "Select SWAP Partition. If using a Swapfile, it will initially set the same size as your RAM."sv;
    static constexpr auto sel_swap_file = "Swapfile"sv;

    std::optional<std::string> answer{};
    {
        std::vector<std::string> temp{"None -"};
        const auto& root_filesystem = gucc::fs::utils::get_mountpoint_fs(utils::get_mountpoint());
        if (!(root_filesystem == "zfs"sv || root_filesystem == "btrfs"sv)) {
            temp.emplace_back("Swapfile -");
        }
        temp.append_range(tui::state().disk.partitions);
        answer = select_partition(temp, sel_swap_body);
    }
    /* clang-format off */
    if (!answer) { return std::nullopt; }
    /* clang-format on */

    utils::SwapSelection selection{};
    if (*answer == "None"sv) {
        selection.type = utils::SwapSelection::Type::None;
        return selection;
    }

    if (*answer == sel_swap_file) {
        const auto total_memory = gucc::memory::total_bytes() / gucc::memory::kMiB;
        std::uint64_t swapfile_bytes{};
        const auto is_swapfile_size = [&swapfile_bytes](std::string_view value) {
            swapfile_bytes = cachyos::installer::parse_size(value).value_or(0);
            return swapfile_bytes >= gucc::memory::kMiB;
        };
        const auto swapfile_size = detail::prompt_validated("Size with a unit: K, M, G or T (e.g. 512M, 4G)"sv, fmt::format(FMT_COMPILE("{} Error: enter at least 1M, e.g. 512M or 4G"), sel_swap_file), is_swapfile_size, fmt::format(FMT_COMPILE("{}M"), total_memory));
        /* clang-format off */
        if (!swapfile_size) { return std::nullopt; }
        /* clang-format on */
        selection.type = utils::SwapSelection::Type::Swapfile;
        // fallocate reads 4G as raw bytes
        selection.swapfile_size = fmt::format(FMT_COMPILE("{}"), swapfile_bytes);
        return selection;
    }

    // Partition swap
    selection.type   = utils::SwapSelection::Type::Partition;
    selection.device = std::move(*answer);

    // Check if partition already has swap
    const auto swap_info   = gucc::disk::query_block_device(selection.device);
    selection.needs_mkswap = !(swap_info && swap_info->fstype == "swap"sv);

    if (selection.needs_mkswap) {
        /* clang-format off */
        if (!detail::yesno_widget(fmt::format(FMT_COMPILE("mkswap {}"), selection.device))) { return std::nullopt; }
        /* clang-format on */
    }

    return selection;
}

// Gather ESP selection from user without applying any changes
auto select_esp_info() noexcept -> std::optional<utils::EspSelection> {
    /* clang-format off */
    if (!tui::state().inputs.ctx.is_uefi()) { return utils::EspSelection{}; }
    /* clang-format on */

    utils::EspSelection selection{};

    static constexpr auto esp_part_body = "Select BOOT partition."sv;
    auto device                         = select_partition(tui::state().disk.partitions, esp_part_body);
    /* clang-format off */
    if (!device) { return std::nullopt; }
    /* clang-format on */
    selection.device = std::move(*device);

    // Check if already fat/vfat
    const bool is_fat_part = gucc::fs::utils::get_device_fstype(selection.device) == "vfat"sv;
    if (!is_fat_part) {
        const auto& content        = fmt::format(FMT_COMPILE("The UEFI partition {} has already been formatted.\n\nReformat? Doing so will erase ALL data already on that partition."), selection.device);
        selection.format_requested = detail::yesno_widget(content);
    }

    // Select mountpoint
    {
        const bool luks                              = tui::state().inputs.ctx.crypto.is_luks;
        static constexpr auto MntUefiBody            = "Select UEFI Mountpoint.\n\n/boot/efi is recommended for multiboot systems. /boot is required for systemd-boot."sv;
        static constexpr auto MntUefiCrypt           = "Select UEFI Mountpoint.\n\n/boot/efi is recommended for multiboot systems and required for full disk encryption. Encrypted /boot is supported only by grub and can lead to slow startup.\n\n/boot is required for systemd-boot and for refind when using encryption."sv;
        const auto& MntUefiMessage                   = !luks ? MntUefiBody : MntUefiCrypt;
        const std::vector<std::string> radiobox_list = {
            "/boot/efi",
            "/boot",
        };
        selection.mountpoint = detail::select_radio(radiobox_list, MntUefiMessage).value_or(std::string{});
    }

    /* clang-format off */
    if (selection.mountpoint.empty()) { return std::nullopt; }
    /* clang-format on */

    return selection;
}

auto select_additional_partition_info() noexcept -> std::optional<utils::AdditionalPartSelection> {
    utils::AdditionalPartSelection selection{};

    // Select device
    {
        std::vector<std::string> temp{"Done -"};
        temp.append_range(tui::state().disk.partitions);

        static constexpr auto extra_part_body = "Select additional partitions in any order, or 'Done' to finish."sv;
        auto device                           = select_partition(temp, extra_part_body);
        /* clang-format off */
        if (!device) { return std::nullopt; }
        /* clang-format on */
        selection.device = std::move(*device);
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

    // Don't give /boot as an example for UEFI systems!
    const auto mnt_examples                = tui::state().inputs.ctx.is_uefi() ? "/home\n/var"sv : "/boot\n/home\n/var"sv;
    static constexpr auto extra_part_body1 = "Specify partition mountpoint. Ensure the name begins with a forward slash (/). Examples include:"sv;
    static constexpr auto mountpoint_error = "Partition cannot be mounted due to a problem with the mountpoint name. A name must be given after a forward slash."sv;
    auto mountpoint                        = detail::prompt_validated(fmt::format(FMT_COMPILE("\n{}\n{}\n"), extra_part_body1, mnt_examples), fmt::format(FMT_COMPILE("{}\n{}\n{}\n"), mountpoint_error, extra_part_body1, mnt_examples), cachyos::installer::is_valid_mountpoint, "/"s);
    /* clang-format off */
    if (!mountpoint) { return std::nullopt; }
    /* clang-format on */
    selection.mountpoint = std::move(*mountpoint);

    // Select mount options
    selection.mount_opts = select_mount_opts(selection.device, selection.fstype, selection.fstype == "skip"sv);
    return selection;
}

auto gather_mount_selections() noexcept -> std::optional<utils::MountSelections> {
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
        if (detail::yesno_widget("Would you like to create subvolumes in it?")) {
            selections.btrfs_subvolumes = utils::select_btrfs_subvolumes(utils::get_mountpoint());
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
    auto out = std::back_inserter(preview);
    preview += "═══ Partition Plan ═══\n\n";

    // Root
    {
        const auto& r      = selections.root;
        const auto& action = r.format_requested
            ? fmt::format(FMT_COMPILE("format as {}"), r.fstype)
            : "keep existing"s;
        fmt::format_to(out, FMT_COMPILE("  Root: {} ({})\n"), r.device, action);
        if (!r.mount_opts.empty()) {
            fmt::format_to(out, FMT_COMPILE("        mount opts: {}\n"), r.mount_opts);
        }
    }

    // BTRFS subvolumes
    if (!selections.btrfs_subvolumes.empty()) {
        preview += "        subvolumes:\n";
        for (const auto& [subvolume, mountpoint] : selections.btrfs_subvolumes) {
            fmt::format_to(out, FMT_COMPILE("          {} → {}\n"), subvolume, mountpoint);
        }
    }

    preview += "\n";

    // ESP
    if (!selections.esp.device.empty()) {
        const auto& action = selections.esp.format_requested ? "format vfat"sv : "keep existing"sv;
        fmt::format_to(out, FMT_COMPILE("  Boot: {} ({}) → {}\n"), selections.esp.device, action, selections.esp.mountpoint);
        preview += "\n";
    }

    // Swap
    switch (selections.swap.type) {
    case utils::SwapSelection::Type::Swapfile:
        fmt::format_to(out, FMT_COMPILE("  Swap: swapfile ({})\n\n"), selections.swap.swapfile_size);
        break;
    case utils::SwapSelection::Type::Partition: {
        const auto& action = selections.swap.needs_mkswap ? "mkswap"sv : "existing"sv;
        fmt::format_to(out, FMT_COMPILE("  Swap: {} ({})\n\n"), selections.swap.device, action);
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
        fmt::format_to(out, FMT_COMPILE("  {} → {} ({})\n"), p.device, p.mountpoint, action);
    }
    if (!selections.additional.empty()) {
        preview += "\n";
    }

    preview += "─────────────────────────────\n";
    preview += "Data on formatted partitions will be lost!\n\n";
    preview += "Proceed with these changes?\n";

    return detail::yesno_widget(preview);
}

void mount_partitions() noexcept {
    // Warn users that they CAN mount partitions without formatting them!
    static constexpr auto content = "IMPORTANT: Partitions can be mounted without formatting them by selecting the 'Do not format' option listed at the top of the file system menu.\n\nEnsure the correct choices for mounting and formatting are made as no warnings will be provided, with the exception of the UEFI boot partition."sv;
    detail::msgbox_widget(content);

    utils::refresh_partition_list(announce_lvm_activation);

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
        detail::msgbox_widget("Failed to apply partition changes. Check the log for details.");
    }
}

void configure_mirrorlist() noexcept {
    static constexpr auto mirrorlist_body = "The pacman configuration file can be edited to enable multilib and other repositories."sv;
    detail::menu_loop(mirrorlist_body, {
                                           {"Edit Pacman Configuration", [] { tui::edit_pacman_conf(); return true; }},
                                           {"Rank mirrors", [] {
                                                if (auto res = gucc::mirrors::rank_mirrors(); !res) {
                                                    spdlog::error("{}", gucc::to_string(res.error()));
                                                }
                                                return true;
                                            }},
                                           {"Back", detail::kLeaveMenu},
                                       });
}

void create_partitions() noexcept {
    static constexpr auto optwipe = "Securely Wipe Device (optional)"sv;
    static constexpr auto optauto = "Automatic Partitioning"sv;

    const std::vector<std::string> menu_entries = {
        std::string{optwipe},
        std::string{optauto},
        "cfdisk",
        "cgdisk",
        "fdisk",
        "gdisk",
        "parted",
    };

    const auto chosen_entry = detail::select_value(menu_entries);
    /* clang-format off */
    if (!chosen_entry) { return; }
    /* clang-format on */

    const auto& selected_entry = *chosen_entry;
    if (selected_entry == optwipe) {
        utils::secure_wipe();
    } else if (selected_entry == optauto) {
        tui::auto_partition();
    } else {
        const std::vector<std::string> argv{selected_entry, tui::state().inputs.ctx.device};
#ifdef NDEVENV
        detail::run_in_terminal(argv);
#else
        spdlog::debug("to be executed: {}", fmt::join(argv, " "));
#endif
    }
}

void install_core_menu() noexcept {
    detail::menu_loop({}, {
                              {"Install Base Packages", guarded(utils::check_mount, tui::install_base)},
                              {"Install Desktop Environment", guarded(utils::check_mount, tui::install_desktop)},
                              {"Install Bootloader", guarded(utils::check_base, tui::install_bootloader)},
                              {"Configure Base", guarded(utils::check_base, tui::config_base_menu)},
                              {"Install Custom Packages", [] { tui::install_cust_pkgs(); return true; }},
                              {"System Tweaks", guarded(utils::check_base, tui::tweaks_menu)},
                              {"Review Configuration Files", guarded(utils::check_base, tui::edit_configs)},
                              {"Chroot into Installation", guarded(utils::check_base, tui::chroot_interactive)},
                              {"Back", detail::kLeaveMenu},
                          });
}

void system_rescue_menu() noexcept {
    static constexpr auto mounted_or_installed = [] { return utils::check_mount() || utils::check_base(); };
    detail::menu_loop({}, {
                              {"Install Hardware Drivers", guarded(mounted_or_installed, tui::install_drivers_menu)},
                              {"Install Bootloader", guarded(mounted_or_installed, tui::install_bootloader)},
                              {"Install Packages", guarded(utils::check_mount, tui::install_cust_pkgs)},
                              {"Remove Packages", guarded(utils::check_base, tui::remove_pkgs)},
                              {"Review Configuration Files", guarded(utils::check_base, tui::edit_configs)},
                              {"Chroot into Installation", guarded(utils::check_base, tui::chroot_interactive)},
                              {"Data Recovery", detail::kLeaveMenu},
                              {"View System Logs", guarded(utils::check_base, tui::logs_menu)},
                              {"Back", detail::kLeaveMenu},
                          });
}

void prep_menu() noexcept {
    detail::menu_loop({}, {
                              {"Set Virtual Console", [] { tui::select_keymap(); return true; }},
                              {"Select Bootloader", [] { tui::select_bootloader_type(); return true; }},
                              {"List Devices (optional)", [] { tui::show_devices(); return true; }},
                              {"Partition Disk", [] {
                                   utils::umount_partitions();
                                   if (tui::select_device()) {
                                       tui::create_partitions();
                                   }
                                   return true;
                               }},
                              {"RAID (optional)", [] { SPDLOG_ERROR("Implement me!"); return true; }},
                              {"Logical Volume Management (optional)", [] { tui::lvm_menu(); return true; }},
                              {"LUKS Encryption (optional)", [] { tui::luks_menu_advanced(); return true; }},
                              {"ZFS (optional)", [] { tui::zfs_menu(); return true; }},
                              {"Mount Partitions", [] { tui::mount_partitions(); return true; }},
                              {"Configure Installer Mirrorlist", [] { tui::configure_mirrorlist(); return true; }},
                              {"Refresh Pacman Keys", [] { tui::refresh_pacman_keys(); return true; }},
                              {"Choose pacman cache", [] { tui::set_cache(); return true; }},
                              {"Back", detail::kLeaveMenu},
                          });
}

void menu_advanced() noexcept {
    detail::menu_loop({}, {
                              {"Prepare Installation", [] { tui::prep_menu(); return true; }},
                              {"Install System", [] {
                                   // Exiting here would quit the installer, so stay in the main menu.
                                   if (!utils::check_mount()) {
                                       detail::msgbox_widget("Mount the partitions first (Prepare Installation -> Mount Partitions)."sv);
                                       return true;
                                   }
                                   tui::install_core_menu();
                                   return true;
                               }},
                              {"System Rescue", [] { tui::system_rescue_menu(); return true; }},
                              {"Done", [] { return !tui::exit_done(); }},
                          });
}

void init() noexcept {
    auto mode = tui::state().menus;
    if (mode == MenuMode::Ask) {
        static const std::vector<std::string> modes{"Simple installation", "Advanced installation"};
        const auto idx = detail::select_from_menu(modes);
        /* clang-format off */
        if (!idx) { return; }
        /* clang-format on */
        mode = (*idx == 0) ? MenuMode::Simple : MenuMode::Advanced;
    }

    if (mode == MenuMode::Simple) {
        tui::menu_simple();
    } else {
        tui::menu_advanced();
    }
}

}  // namespace tui
