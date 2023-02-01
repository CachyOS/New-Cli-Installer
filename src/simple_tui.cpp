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
        "ext4",
        "btrfs",
        "xfs",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    bool success{};
    auto ok_callback = [&] {
        const auto& src      = menu_entries[static_cast<std::size_t>(selected)];
        const auto& lines    = utils::make_multiline(src, false, ' ');
        const auto& file_sys = lines[0];
        /*if (file_sys == "zfs") {
            // NOTE: We don't have automatic zfs partitioning,
            // in HEADLESS mode.
            tui::zfs_auto();
        }*/
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

void make_esp(const auto& part_name) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    const auto& sys_info  = std::get<std::string>(config_data["SYSTEM"]);

    /* clang-format off */
    if (sys_info != "UEFI") { return; }
    /* clang-format on */

    auto& partition           = std::get<std::string>(config_data["PARTITION"]);
    partition                 = part_name;
    config_data["UEFI_MOUNT"] = "/boot";
    config_data["UEFI_PART"]  = partition;

    spdlog::debug("Formating boot partition with fat/vfat!");
#ifdef NDEVENV
    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& uefi_mount      = std::get<std::string>(config_data["UEFI_MOUNT"]);
    const auto& path_formatted  = fmt::format(FMT_COMPILE("{}{}"), mountpoint_info, uefi_mount);

    utils::exec(fmt::format(FMT_COMPILE("mkfs.vfat -F32 {} &>/dev/null"), partition));
    utils::exec(fmt::format(FMT_COMPILE("mkdir -p {}"), path_formatted));
    utils::exec(fmt::format(FMT_COMPILE("mount {} {}"), partition, path_formatted));
#endif
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
    const auto& ready_parts     = std::get<std::vector<std::string>>(config_data["READY_PARTITIONS"]);

    const auto& hostname = std::get<std::string>(config_data["HOSTNAME"]);
    const auto& locale   = std::get<std::string>(config_data["LOCALE"]);
    const auto& xkbmap   = std::get<std::string>(config_data["XKBMAP"]);
    const auto& timezone = std::get<std::string>(config_data["TIMEZONE"]);

    const auto& user_name  = std::get<std::string>(config_data["USER_NAME"]);
    const auto& user_pass  = std::get<std::string>(config_data["USER_PASS"]);
    const auto& user_shell = std::get<std::string>(config_data["USER_SHELL"]);
    const auto& root_pass  = std::get<std::string>(config_data["ROOT_PASS"]);

    const auto& kernel       = std::get<std::string>(config_data["KERNEL"]);
    const auto& desktop      = std::get<std::string>(config_data["DE"]);
    const auto& bootloader   = std::get<std::string>(config_data["BOOTLOADER"]);
    const auto& drivers_type = std::get<std::string>(config_data["DRIVERS_TYPE"]);
    const auto& post_install = std::get<std::string>(config_data["POST_INSTALL"]);

    if (device_info.empty()) {
        tui::select_device();
    }

    utils::auto_partition();

    // LVM Detection. If detected, activate.
    utils::lvm_detect();

    // Ensure partitions are unmounted (i.e. where mounted previously)
    config_data["INCLUDE_PART"] = "part\\|lvm\\|crypt";
    utils::umount_partitions();

    // We need to remount the zfs filesystems that have defined mountpoints already
    utils::exec("zfs mount -aO &>/dev/null");

    // Get list of available partitions
    utils::find_partitions();

    // Filter out partitions that have already been mounted and partitions that just contain crypt or zfs devices
    auto ignore_part = utils::list_mounted();
    ignore_part += utils::zfs_list_devs();
    ignore_part += utils::list_containing_crypt();

    std::vector<std::pair<std::string, double>> parts{};

    const auto& partitions = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);
    for (const auto& partition : partitions) {
        /* clang-format off */
        if (!partition.starts_with(device_info)) { continue; }
        /* clang-format on */
        const auto& partition_stat = utils::make_multiline(partition, false, ' ');
        const auto& part_name      = partition_stat[0];
        const auto& part_size      = partition_stat[1];

        if (part_size == "512M") {
            make_esp(part_name);
            utils::get_cryptroot();
            utils::get_cryptboot();
            spdlog::info("boot partition: name={}", part_name);
            continue;
        }

        const auto& size_end_pos = std::find_if(part_size.begin(), part_size.end(), [](auto&& ch) { return std::isalpha(ch); });
        const auto& number_len   = std::distance(part_size.begin(), size_end_pos);
        const std::string_view number_str{part_size.data(), static_cast<std::size_t>(number_len)};
        const std::string_view unit{part_size.data() + number_len, part_size.size() - static_cast<std::size_t>(number_len)};

        const double number       = utils::to_floating(number_str);
        const auto& converted_num = utils::convert_unit(number, unit);
        parts.push_back(std::pair<std::string, double>{part_name, converted_num});
    }

    const auto& root_part    = std::max_element(parts.begin(), parts.end(), [](auto&& lhs, auto&& rhs) { return lhs.second < rhs.second; })->first;
    config_data["PARTITION"] = root_part;
    config_data["ROOT_PART"] = root_part;

    // Reset the mountpoint variable, in case this is the second time through this menu and old state is still around
    config_data["MOUNT"] = "";

    // Target FS
    if (fs_name.empty()) {
        select_filesystem();
    } else {
        utils::select_filesystem(fs_name);
    }
    tui::mount_current_partition(true);

    // If the root partition is btrfs, offer to create subvolumes
    if (utils::exec(fmt::format(FMT_COMPILE("findmnt -no FSTYPE \"{}\""), mountpoint)) == "btrfs") {
        // Check if there are subvolumes already on the btrfs partition
        const auto& subvolumes       = fmt::format(FMT_COMPILE("btrfs subvolume list \"{}\""), mountpoint);
        const auto& subvolumes_count = utils::exec(fmt::format(FMT_COMPILE("{} | wc -l"), subvolumes));
        const auto& lines_count      = utils::to_int(subvolumes_count.data());
        if (lines_count > 1) {
            // Pre-existing subvolumes and user wants to mount them
            utils::mount_existing_subvols({root_part, root_part});
        } else {
            utils::btrfs_create_subvols({.root = root_part, .mount_opts = mount_opts_info}, "automatic", true);
        }
    }

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

    // Install process
    if (kernel.empty()) {
        tui::install_base();
    } else {
        utils::install_base(kernel);
    }
    if (desktop.empty()) {
        tui::install_desktop();
    } else {
        utils::install_desktop(desktop);
    }

    if (bootloader.empty()) {
        tui::install_bootloader();
    } else {
        utils::install_bootloader(bootloader);
    }

#ifdef NDEVENV
    utils::arch_chroot(fmt::format(FMT_COMPILE("mhwd -a pci {} 0300"), drivers_type));
    std::ofstream{fmt::format(FMT_COMPILE("{}/.video_installed"), mountpoint)};
#endif

    if (!post_install.empty()) {
        spdlog::info("Running post-install script...");
        // Flush before executing post-install script,
        // so that output will be synchronized.
        auto logger = spdlog::get("cachyos_logger");
        logger->flush();
        utils::exec(fmt::format(FMT_COMPILE("{} &>>/tmp/cachyos-install.log"), post_install), true);
    }

    tui::exit_done();

    fmt::print("┌{0:─^{5}}┐\n"
               "│{1: ^{5}}│\n"
               "│{2: ^{5}}│\n"
               "│{3: ^{5}}│\n"
               "│{4: ^{5}}│\n"
               "└{0:─^{5}}┘\n",
        "",
        fmt::format(FMT_COMPILE("Mountpoint: {}"), mountpoint),
        fmt::format(FMT_COMPILE("Your device: {}"), device_info),
        fmt::format(FMT_COMPILE("Filesystem: {}"), fs_name),
        fmt::format(FMT_COMPILE("Filesystem opts: {}"), mount_opts_info), 80);

    fmt::print("┌{0:─^{5}}┐\n"
               "│{1: ^{5}}│\n"
               "│{2: ^{5}}│\n"
               "│{3: ^{5}}│\n"
               "│{4: ^{5}}│\n"
               "└{0:─^{5}}┘\n",
        "",
        fmt::format(FMT_COMPILE("Kernel: {}"), kernel),
        fmt::format(FMT_COMPILE("Desktop: {}"), desktop),
        fmt::format(FMT_COMPILE("Drivers type: {}"), drivers_type),
        fmt::format(FMT_COMPILE("Bootloader: {}"), bootloader), 80);
}

}  // namespace tui
