#include "simple_tui.hpp"
#include "disk.hpp"
#include "global_storage.hpp"
#include "tui.hpp"
#include "utils.hpp"
#include "widgets.hpp"

// import gucc
#include "gucc/btrfs.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/partitioning.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/zfs.hpp"

#include <cstdlib>      // for setenv
#include <sys/mount.h>  // for mount

#include <algorithm>   // for copy
#include <filesystem>  // for exists, is_directory
#include <fstream>     // for ofstream
#include <ranges>      // for ranges::*
#include <string>      // for basic_string
#include <thread>      // for thread

/* clang-format off */
#include <ftxui/component/component.hpp>           // for Renderer, Button
#include <ftxui/component/screen_interactive.hpp>  // for Component, ScreenI...
#include <ftxui/dom/elements.hpp>                  // for operator|, size
/* clang-format on */

#include <fmt/compile.h>
#include <fmt/ranges.h>

using namespace ftxui;
using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

// Structure to hold all user selections
struct UserSelections {
    bool server_mode;

    std::string device;
    std::string filesystem;
    std::string bootloader;
    std::string hostname;
    std::string locale;
    std::string xkbmap;
    std::string timezone;
    std::string user_name;
    std::string user_pass;
    std::string user_shell;
    std::string root_pass;
    std::string kernel;
    std::string desktop;
    std::string post_install;
    std::string mount_options;
    std::vector<std::string> ready_partitions;
};

// Set static list of filesystems rather than on-the-fly. Partially as most require additional flags, and
// partially because some don't seem to be viable.
auto select_filesystem() noexcept -> std::string {
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
    std::string selected_fs{};
    bool success{};
    auto ok_callback = [&] {
        const auto& file_sys = menu_entries[static_cast<std::size_t>(selected)];
        /*if (file_sys == "zfs"sv) {
            // NOTE: We don't have automatic zfs partitioning,
            // in HEADLESS mode.
            tui::zfs_auto();
        }*/
        selected_fs = file_sys;
        // utils::select_filesystem(file_sys.c_str());
        success = true;
        screen.ExitLoopClosure()();
    };
    static constexpr auto filesystem_body = "\nSelect your filesystem\n";
    const auto& content_size              = size(HEIGHT, LESS_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    tui::detail::menu_widget(menu_entries, ok_callback, &selected, &screen, filesystem_body, {size(HEIGHT, LESS_THAN, 18), content_size});

    if (!success) {
        return {"btrfs"};
    }

    return selected_fs;
}

auto select_bootloader(std::string_view sys_info) noexcept -> std::string {
    const auto& menu_entries = [](const auto& bios_mode) -> std::vector<std::string> {
        const auto& available_bootloaders = utils::available_bootloaders(bios_mode);

        std::vector<std::string> result{};
        result.reserve(available_bootloaders.size());

        std::ranges::transform(available_bootloaders, std::back_inserter(result),
            [=](const std::string_view& entry) -> std::string { return std::string(entry); });
        return result;
    }(sys_info);

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    bool success{};
    std::string selected_bootloader{};
    auto ok_callback = [&] {
        selected_bootloader = menu_entries[static_cast<std::size_t>(selected)];
        success             = true;
        screen.ExitLoopClosure()();
    };

    static constexpr auto filesystem_body = "\nSelect your bootloader\n"sv;
    const auto& content_size              = size(HEIGHT, LESS_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    tui::detail::menu_widget(menu_entries, ok_callback, &selected, &screen, filesystem_body, {size(HEIGHT, LESS_THAN, 18), content_size});

    if (!success) {
        // default bootloaders
        selected_bootloader = (sys_info == "UEFI"sv) ? "systemd-boot"sv : "grub"sv;
    }

    return selected_bootloader;
}

void make_esp(std::vector<gucc::fs::Partition>& partitions, std::string_view part_name, std::string_view bootloader_name, bool reformat_part = true, std::string_view boot_part_mountpoint = "(empty)"sv) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    const auto& sys_info  = std::get<std::string>(config_data["SYSTEM"]);

    /* clang-format off */
    if (sys_info != "UEFI"sv) { return; }
    /* clang-format on */

    auto& partition = std::get<std::string>(config_data["PARTITION"]);
    partition       = std::string{part_name};

    const auto& uefi_mount    = (boot_part_mountpoint == "(empty)"sv) ? utils::bootloader_default_mount(bootloader_name, sys_info) : boot_part_mountpoint;
    config_data["UEFI_MOUNT"] = std::string{uefi_mount};
    config_data["UEFI_PART"]  = partition;

    // If it is already a fat/vfat partition...
    const auto& is_fat_part = gucc::utils::exec_checked(fmt::format(FMT_COMPILE("fsck -N {} | grep -q fat"), partition));
    if (!is_fat_part && reformat_part) {
#ifdef NDEVENV
        gucc::utils::exec(fmt::format(FMT_COMPILE("mkfs.vfat -F32 {} &>/dev/null"), partition));
#endif
        spdlog::debug("Formating boot partition with fat/vfat!");
    }

    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& part_mountpoint = fmt::format(FMT_COMPILE("{}{}"), mountpoint_info, uefi_mount);
#ifdef NDEVENV
    gucc::utils::exec(fmt::format(FMT_COMPILE("mkdir -p {}"), part_mountpoint));
    gucc::utils::exec(fmt::format(FMT_COMPILE("mount {} {}"), partition, part_mountpoint));
#endif

    // TODO(vnepogodin): handle boot partition mount options
    const auto& part_fs   = gucc::fs::utils::get_mountpoint_fs(part_mountpoint);
    auto boot_part_struct = gucc::fs::Partition{.fstype = part_fs, .mountpoint = std::string{uefi_mount}, .device = partition, .mount_opts = "defaults"s};

    const auto& part_uuid     = gucc::fs::utils::get_device_uuid(boot_part_struct.device);
    boot_part_struct.uuid_str = part_uuid;

    // TODO(vnepogodin): handle luks information

    utils::dump_partition_to_log(boot_part_struct);

    // insert boot partition
    partitions.emplace_back(std::move(boot_part_struct));
}

auto make_partitions_prepared(std::string_view bootloader, std::string_view root_fs, std::string_view mount_opts_info, const auto& ready_parts) -> bool {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    /* clang-format off */
    if (ready_parts.empty()) { spdlog::error("Invalid use! ready parts empty."); return false; }
    /* clang-format on */

    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);

    std::vector<gucc::fs::Partition> partitions{};

    std::string root_part{};
    for (auto&& ready_part : ready_parts) {
        auto part_info = gucc::utils::make_multiline(ready_part, false, '\t');

        auto part_name       = part_info[0];
        auto part_mountpoint = part_info[1];
        auto part_size       = part_info[2];
        auto part_fs         = part_info[3];
        auto part_type       = part_info[4];

        spdlog::debug("\n========\npart name := '{}'\npart mountpoint := '{}'\npart size := '{}'\npart fs := '{}'\npart type := '{}'\n========\n",
            part_name, part_mountpoint, part_size, part_fs, part_type);

        if (part_type == "boot"sv) {
            config_data["UEFI_MOUNT"] = part_mountpoint;
            config_data["UEFI_PART"]  = part_name;
            make_esp(partitions, part_name, bootloader, true, part_mountpoint);
            utils::get_cryptroot();
            utils::get_cryptboot();
            spdlog::info("boot partition: name={}", part_name);
            continue;
        } else if (part_type == "root"sv) {
            config_data["PARTITION"] = part_name;
            config_data["ROOT_PART"] = part_name;
            config_data["MOUNT"]     = part_mountpoint;
            root_part                = part_name;
            spdlog::info("root partition: {}", part_name);

            utils::select_filesystem(root_fs);
            tui::mount_current_partition(true);

            auto part_struct = gucc::fs::Partition{.fstype = part_fs, .mountpoint = part_mountpoint, .device = root_part, .mount_opts = std::string{mount_opts_info}};

            const auto& part_uuid = gucc::fs::utils::get_device_uuid(part_struct.device);
            part_struct.uuid_str  = part_uuid;

            // get luks information about the current partition
            const auto& luks_name = std::get<std::string>(config_data["LUKS_NAME"]);
            const auto& luks_uuid = std::get<std::string>(config_data["LUKS_UUID"]);
            if (!luks_name.empty()) {
                part_struct.luks_mapper_name = luks_name;
            }
            if (!luks_uuid.empty()) {
                part_struct.luks_uuid = luks_uuid;
            }

            utils::dump_partition_to_log(part_struct);

            partitions.emplace_back(std::move(part_struct));

            // If the root partition is btrfs, offer to create subvolumes
            if (root_fs == "btrfs"sv) {
                // Check if there are subvolumes already on the btrfs partition
                const auto& mount_dir  = fmt::format(FMT_COMPILE("{}{}"), mountpoint_info, part_mountpoint);
                const auto& subvolumes = gucc::utils::exec(fmt::format(FMT_COMPILE("btrfs subvolume list '{}' 2>/dev/null | cut -d' ' -f9"), mount_dir));
                if (!subvolumes.empty()) {
                    // Pre-existing subvolumes and user wants to mount them
                    if (!utils::mount_existing_subvols(partitions)) {
                        spdlog::error("Failed to mount existing btrfs subvolumes");
                    }
                } else {
                    // Create default subvolumes automatically
                    const auto& default_subvols = utils::default_btrfs_subvolumes();
#ifdef NDEVENV
                    gucc::utils::exec(R"(mount | grep 'on /mnt ' | grep -Po '(?<=\().*(?=\))' > /tmp/.root_mount_options)"sv);
                    if (!gucc::fs::btrfs_create_subvols(default_subvols, root_part, mountpoint_info, mount_opts_info)) {
                        spdlog::error("Failed to create subvolumes automatically");
                    }
#endif
                    if (!gucc::fs::btrfs_append_subvolumes(partitions, default_subvols)) {
                        spdlog::error("Failed to append btrfs subvolumes into partition scheme");
                    }
                }
            }
            continue;
        } else if (part_type == "additional"sv) {
            config_data["MOUNT"]     = part_mountpoint;
            config_data["PARTITION"] = part_name;
            spdlog::info("additional partition: {}", part_name);

            utils::select_filesystem(part_fs);
            tui::mount_current_partition(true);

            auto part_struct = gucc::fs::Partition{.fstype = part_fs, .mountpoint = part_mountpoint, .device = part_name, .mount_opts = std::string{mount_opts_info}};

            const auto& part_uuid = gucc::fs::utils::get_device_uuid(part_struct.device);
            part_struct.uuid_str  = part_uuid;

            // get luks information about the current partition
            const auto& luks_name = std::get<std::string>(config_data["LUKS_NAME"]);
            const auto& luks_uuid = std::get<std::string>(config_data["LUKS_UUID"]);
            if (!luks_name.empty()) {
                part_struct.luks_mapper_name = luks_name;
            }
            if (!luks_uuid.empty()) {
                part_struct.luks_uuid = luks_uuid;
            }

            utils::dump_partition_to_log(part_struct);

            partitions.emplace_back(std::move(part_struct));

            // Determine if a separate /boot is used.
            // 0 = no separate boot,
            // 1 = separate non-lvm boot,
            // 2 = separate lvm boot. For Grub configuration
            if (part_mountpoint == "/boot"sv) {
                config_data["LVM_SEP_BOOT"] = 1;

                const auto& cmd        = fmt::format(FMT_COMPILE("lsblk -lno TYPE {} | grep -q 'lvm'"), part_name);
                const bool is_boot_lvm = gucc::utils::exec_checked(cmd);
                if (is_boot_lvm) {
                    config_data["LVM_SEP_BOOT"] = 2;
                }
            }
            continue;
        }
    }

    utils::dump_partitions_to_log(partitions);
    const auto& device_info = std::get<std::string>(config_data["DEVICE"]);
    const auto& sys_info    = std::get<std::string>(config_data["SYSTEM"]);
    spdlog::info("{}\n", gucc::disk::preview_partition_schema(partitions, device_info, sys_info == "UEFI"sv));
    return true;
}

auto get_root_part(std::string_view device_info) noexcept -> std::string {
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& partitions = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);

    std::vector<std::pair<std::string, double>> parts{};
    for (const auto& partition : partitions) {
        /* clang-format off */
        if (!partition.starts_with(device_info)) { continue; }
        /* clang-format on */
        const auto& partition_stat = gucc::utils::make_multiline(partition, false, ' ');
        const auto& part_name      = partition_stat[0];
        const auto& part_size      = partition_stat[1];

        if (part_size == "512M"sv || part_size == "1G"sv || part_size == "2G"sv) {
            continue;
        }

        const auto& size_end_pos = std::ranges::find_if(part_size, [](auto&& ch) { return std::isalpha(ch); });
        const auto& number_len   = std::ranges::distance(part_size.begin(), size_end_pos);
        const std::string_view number_str{part_size.data(), static_cast<std::size_t>(number_len)};
        const std::string_view unit{part_size.data() + number_len, part_size.size() - static_cast<std::size_t>(number_len)};

        const double number       = utils::to_floating(number_str);
        const auto& converted_num = utils::convert_unit(number, unit);
        parts.emplace_back(part_name, converted_num);
    }
    return std::max_element(parts.begin(), parts.end(), [](auto&& lhs, auto&& rhs) { return lhs.second < rhs.second; })->first;
}

auto auto_make_partitions(std::string_view device_info, std::string_view root_fs, std::string_view mount_opts_info) noexcept -> std::vector<std::string> {
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& bootloader = std::get<std::string>(config_data["BOOTLOADER"]);

    // TODO(vnepogodin): its currently implemented as "erase disk" with our predefined scheme.
    // implement for already mounted and already partitioned
    auto root_part = get_root_part(device_info);
    std::vector<std::pair<std::string, double>> parts{};
    std::vector<std::string> ready_parts{};

    /*{
        auto&& part_data = fmt::format(FMT_COMPILE("{}\t{}\t{}\t{}\t{}"), part_name,
            "/", "", root_fs, "root");
        ready_parts.emplace_back(std::move(part_data));
    }
    const auto& partitions = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);
    for (const auto& partition : partitions) {*/
    /* clang-format off */
        //if (!partition.starts_with(device_info)) { continue; }
    /* clang-format on */
    /*const auto& partition_stat = gucc::utils::make_multiline(partition, false, ' ');
    const auto& part_name      = partition_stat[0];
    const auto& part_size      = partition_stat[1];

    auto part_type{"additional"};
    if (part_size == "512M"sv || part_size == "1G"sv || part_size == "2G"sv) {
        //make_esp(part_name, bootloader);
        spdlog::info("boot partition: name={}", part_name);
        continue;
    }

    auto&& part_data = fmt::format(FMT_COMPILE("{}\t{}\t{}\t{}\t{}"), part_name,
        partition_map["mountpoint"].GetString(), partition_map["size"].GetString(), part_fs_name, part_type);
    ready_parts.emplace_back(std::move(part_data));
}
auto root_part = std::max_element(parts.begin(), parts.end(), [](auto&& lhs, auto&& rhs) { return lhs.second < rhs.second; })->first;
ready_parts.push_back();*/
    return ready_parts;
}

}  // namespace

namespace tui {

UserSelections gather_user_selections() noexcept {
    UserSelections selections;
    auto* config_instance       = Config::instance();
    auto& config_data           = config_instance->data();
    const auto& mountpoint      = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& device_info     = std::get<std::string>(config_data["DEVICE"]);
    auto& fs_name               = std::get<std::string>(config_data["FILESYSTEM_NAME"]);
    const auto& mount_opts_info = std::get<std::string>(config_data["MOUNT_OPTS"]);
    auto& ready_parts           = std::get<std::vector<std::string>>(config_data["READY_PARTITIONS"]);

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
    auto& bootloader         = std::get<std::string>(config_data["BOOTLOADER"]);
    const auto& post_install = std::get<std::string>(config_data["POST_INSTALL"]);
    const auto& server_mode  = std::get<std::int32_t>(config_data["SERVER_MODE"]);

    if (device_info.empty()) {
        tui::select_device();
        selections.device = std::get<std::string>(config_data["DEVICE"]);
    } else {
        selections.device = device_info;
    }

    utils::auto_partition();
    // LVM Detection. If detected, activate.
    utils::lvm_detect();

    // Ensure partitions are unmounted (i.e. where mounted previously)
    config_data["INCLUDE_PART"] = "part\\|lvm\\|crypt";
    utils::umount_partitions();

    // We need to remount the zfs filesystems that have defined mountpoints already
    gucc::utils::exec("zfs mount -aO &>/dev/null");

    // Get list of available partitions
    utils::find_partitions();

    // Filter out partitions that have already been mounted and partitions that just contain crypt or zfs devices
    auto ignore_part = utils::list_mounted();
    ignore_part += gucc::fs::zfs_list_devs();
    ignore_part += utils::list_containing_crypt();

    // We must have bootloader before running partitioning step
    if (bootloader.empty()) {
        const auto& sys_info  = std::get<std::string>(config_data["SYSTEM"]);
        selections.bootloader = select_bootloader(sys_info);
        bootloader            = selections.bootloader;
    } else {
        selections.bootloader = bootloader;
    }

    // Target FS
    if (fs_name.empty()) {
        selections.filesystem = select_filesystem();
        fs_name               = selections.filesystem;
    } else {
        selections.filesystem = fs_name;
    }

    if (ready_parts.empty()) {
        utils::select_filesystem(selections.filesystem);
    }

    // tui::mount_current_partition(true);
    // TODO(vnepogodin): implement automatic partitioning if there is no config partition scheme.
    // something similar to default partioning table created by calamares is fine.
    if (ready_parts.empty()) {
        spdlog::info("TODO: auto make layout(ready parts)!");
        // ready_parts = auto_make_partitions(device_info, fs_name, mount_opts_info);
    } else {
        selections.ready_partitions = ready_parts;
    }

    selections.hostname = hostname;
    selections.locale   = locale;
    selections.xkbmap   = xkbmap;
    selections.timezone = timezone;

    selections.user_name  = user_name;
    selections.user_pass  = user_pass;
    selections.user_shell = user_shell;
    selections.root_pass  = root_pass;

    selections.kernel        = kernel;
    selections.desktop       = desktop;
    selections.post_install  = post_install;
    selections.server_mode   = server_mode != 0;
    selections.mount_options = mount_opts_info;

    return selections;
}

void apply_user_selections(const UserSelections& selections) noexcept {
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    config_data["INCLUDE_PART"] = "part\\|lvm\\|crypt";
    utils::umount_partitions();

    if (!make_partitions_prepared(selections.bootloader, selections.filesystem, selections.mount_options, selections.ready_partitions)) {
        utils::umount_partitions();
        return;
    }

    // If the root partition is btrfs, offer to create subvolumes
    /*if (gucc::fs::utils::get_mountpoint_fs(mountpoint) == "btrfs"sv) {
        // Check if there are subvolumes already on the btrfs partition
        const auto& subvolumes       = fmt::format(FMT_COMPILE("btrfs subvolume list \"{}\" 2>/dev/null"), mountpoint);
        const auto& subvolumes_count = gucc::utils::exec(fmt::format(FMT_COMPILE("{} | wc -l"), subvolumes));
        const auto& lines_count      = utils::to_int(subvolumes_count.data());
        if (lines_count > 1) {
            // Pre-existing subvolumes and user wants to mount them
            utils::mount_existing_subvols({root_part, root_part});
        } else {
            utils::btrfs_create_subvols({.root = root_part, .mount_opts = mount_opts_info}, "automatic", true);
        }
    }*/

    // at this point we should have everything already mounted
    utils::generate_fstab();

    if (selections.hostname.empty()) {
        tui::set_hostname();
    } else {
        utils::set_hostname(selections.hostname);
    }

    if (selections.locale.empty()) {
        tui::set_locale();
    } else {
        utils::set_locale(selections.locale);
    }

    if (selections.xkbmap.empty()) {
        tui::set_xkbmap();
    } else {
        utils::set_xkbmap(selections.xkbmap);
    }

    if (selections.timezone.empty()) {
        tui::set_timezone();
    } else {
        utils::set_timezone(selections.timezone);
    }

    utils::set_hw_clock("utc"sv);

    if (selections.root_pass.empty()) {
        tui::set_root_password();
    } else {
        utils::set_root_password(selections.root_pass);
    }

    if (selections.user_name.empty()) {
        tui::create_new_user();
    } else {
        utils::create_new_user(selections.user_name, selections.user_pass, selections.user_shell);
    }

    // Install process
    if (selections.kernel.empty()) {
        tui::install_base();
    } else {
        utils::install_base(selections.kernel);
    }

    if (!selections.server_mode) {
        if (selections.desktop.empty()) {
            tui::install_desktop();
        } else {
            utils::install_desktop(selections.desktop);
        }
    }

    if (selections.bootloader.empty()) {
        tui::install_bootloader();
    } else {
        utils::install_bootloader(selections.bootloader);
    }

#ifdef NDEVENV
    if (!selections.server_mode) {
        utils::arch_chroot("chwd -a");
        std::ofstream{fmt::format(FMT_COMPILE("{}/.video_installed"), mountpoint)};
    }
#endif

    if (!selections.post_install.empty()) {
        spdlog::info("Running post-install script...");
        // Flush before executing post-install script,
        // so that output will be synchronized.
        auto logger = spdlog::get("cachyos_logger");
        logger->flush();
        gucc::utils::exec(fmt::format(FMT_COMPILE("{} &>>/tmp/cachyos-install.log"), selections.post_install), true);
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
        fmt::format(FMT_COMPILE("Your device: {}"), selections.device),
        fmt::format(FMT_COMPILE("Filesystem: {}"), selections.filesystem),
        fmt::format(FMT_COMPILE("Filesystem opts: {}"), selections.mount_options), 80);

    fmt::print("┌{0:─^{4}}┐\n"
               "│{1: ^{4}}│\n"
               "│{2: ^{4}}│\n"
               "│{3: ^{4}}│\n"
               "└{0:─^{4}}┘\n",
        "",
        fmt::format(FMT_COMPILE("Kernel: {}"), selections.kernel),
        fmt::format(FMT_COMPILE("Desktop: {}"), (!selections.server_mode) ? selections.desktop : "---"),
        fmt::format(FMT_COMPILE("Bootloader: {}"), selections.bootloader), 80);
}

void menu_simple() noexcept {
    auto selections = gather_user_selections();
    apply_user_selections(selections);
}

}  // namespace tui
