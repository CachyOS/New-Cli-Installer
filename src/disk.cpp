#include "disk.hpp"
#include "global_storage.hpp"
#include "misc.hpp"
#include "utils.hpp"
#include "widgets.hpp"

// import installer-lib
#include "cachyos/disk.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/btrfs.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/mount_partitions.hpp"
#include "gucc/partition.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/swap.hpp"
#include "gucc/system_query.hpp"

#include <algorithm>   // for find_if, transform
#include <filesystem>  // for create_directories
#include <ranges>      // for ranges::*

#include <ftxui/component/component.hpp>           // for Renderer, Button
#include <ftxui/component/component_options.hpp>   // for ButtonOption
#include <ftxui/component/screen_interactive.hpp>  // for Component, ScreenI...
#include <ftxui/dom/elements.hpp>                  // for operator|, size
#include <sys/mount.h>                             // for umount

#include <fmt/compile.h>
#include <fmt/core.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;
using namespace std::string_literals;

namespace {

constexpr auto is_root_btrfs_part(const gucc::fs::Partition& part) noexcept -> bool {
    return (part.mountpoint == "/"sv) && (part.fstype == "btrfs"sv);
}

// TODO(vnepogodin): refactor that out of this file
constexpr auto find_root_btrfs_part(auto&& parts) noexcept {
    return std::ranges::find_if(parts,
        [](auto&& part) { return is_root_btrfs_part(part); });
}

}  // namespace

namespace utils {

auto default_btrfs_subvolumes() noexcept -> std::vector<gucc::fs::BtrfsSubvolume> {
    return cachyos::installer::default_btrfs_subvolumes();
}

auto apply_btrfs_subvolumes(const std::vector<gucc::fs::BtrfsSubvolume>& subvolumes, const RootPartitionSelection& selection, std::string_view mountpoint_info, std::vector<gucc::fs::Partition>& partition_schema) noexcept -> bool {
    if (subvolumes.empty() && !selection.format_requested) {
        // check for existing subvols if we are not formatting the disk
        const auto& exist_subvols = gucc::utils::exec(fmt::format(FMT_COMPILE("btrfs subvolume list '{}' 2>/dev/null | cut -d' ' -f9"), mountpoint_info));
        if (!exist_subvols.empty()) {
            spdlog::info("Found existing btrfs subvolumes, mounting them automatically");
            if (!utils::mount_existing_subvols(partition_schema)) {
                spdlog::error("Failed to mount existing btfs subvolumes");
                return false;
            }
        }
        return true;
    }
    // simply exit
    /* clang-format off */
    if (subvolumes.empty()) { return true; }
    /* clang-format on */

#ifdef NDEVENV
    {
        const cachyos::installer::RootPartitionSelection lib_selection{
            .device           = selection.device,
            .fstype           = selection.fstype,
            .mkfs_command     = selection.mkfs_command,
            .mount_opts       = selection.mount_opts,
            .format_requested = selection.format_requested,
        };
        auto result = cachyos::installer::apply_btrfs_subvolumes(subvolumes, lib_selection, mountpoint_info, partition_schema);
        if (!result) {
            spdlog::error("{}", result.error());
            return false;
        }
    }
#else
    spdlog::info("[DRY-RUN] Would create {} btrfs subvolumes", subvolumes.size());
    if (!gucc::fs::btrfs_append_subvolumes(partition_schema, subvolumes)) {
        spdlog::error("Failed to append btrfs subvolumes into partition scheme");
        return false;
    }
#endif

    // find root part schema
    const auto& root_part = find_root_btrfs_part(partition_schema);
    utils::dump_partition_to_log(*root_part);
    return true;
}

auto select_btrfs_subvolumes(std::string_view root_mountpoint) noexcept -> std::vector<gucc::fs::BtrfsSubvolume> {
    const std::vector<std::string> menu_entries = {
        "automatic",
        "manual",
    };
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    std::string btrfsvols_mode{};
    auto ok_callback = [&] {
        btrfsvols_mode = menu_entries[static_cast<std::size_t>(selected)];
        screen.ExitLoopClosure()();
    };
    static constexpr auto btrfsvols_body = "\nAutomatic mode\nis designed to allow integration\nwith snapper, non-recursive snapshots,\nseparating system and user data and\nrestoring snapshots without losing data.\n"sv;
    tui::detail::menu_widget(menu_entries, ok_callback, &selected, &screen, btrfsvols_body);

    /* clang-format off */
    if (btrfsvols_mode.empty()) { return {}; }
    /* clang-format on */

    if (btrfsvols_mode == "automatic"sv) {
        return default_btrfs_subvolumes();
    }

    // Ask user for subvolume names and their mountpoints
    std::string subvols{"/@ /@home /@cache"};
    static constexpr auto subvols_body = "\nInput names of the subvolumes separated by spaces.\nThe first one will be used for mounting /.\n"sv;
    if (!tui::detail::inputbox_widget(subvols, subvols_body, size(ftxui::HEIGHT, ftxui::GREATER_THAN, 4))) {
        return {};
    }
    subvols = std::string{gucc::utils::trim(subvols)};

    auto subvol_list = gucc::utils::make_multiline(subvols, false, ' ');
    /* clang-format off */
    if (subvol_list.empty()) { return {}; }
    /* clang-format on */

    // Make the first subvolume with mountpoint /
    std::vector<gucc::fs::BtrfsSubvolume> subvolumes{
        gucc::fs::BtrfsSubvolume{.subvolume = std::move(subvol_list[0]), .mountpoint = "/"s},
    };

    // Remove the first subvolume from the subvolume list
    subvol_list.erase(subvol_list.begin());

    // Ask mountpoint for each remaining subvolume
    for (auto&& subvol : subvol_list) {
        const auto& content = fmt::format(FMT_COMPILE("\nInput mountpoint of\nthe subvolume {}\nas it would appear\nin installed system\n(without prepending {}).\n"), subvol, root_mountpoint);
        std::string mountpoint{"/home"};
        if (!tui::detail::inputbox_widget(mountpoint, content, size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
            return {};
        }
        mountpoint = std::string{gucc::utils::trim(mountpoint)};
        subvolumes.push_back(gucc::fs::BtrfsSubvolume{.subvolume = std::move(subvol), .mountpoint = std::move(mountpoint)});
    }

    return subvolumes;
}

auto mount_existing_subvols(std::vector<gucc::fs::Partition>& partitions) noexcept -> bool {
    auto root_part = find_root_btrfs_part(partitions);
    if (root_part == std::ranges::end(partitions)) {
        spdlog::error("mount_existing_subvols: unable to find btrfs root part");
        return false;
    }

    const bool is_ssd   = gucc::disk::is_device_ssd(root_part->device);
    const auto& fs_opts = gucc::fs::get_default_mount_opts(gucc::fs::FilesystemType::Btrfs, is_ssd);

    auto* config_instance       = Config::instance();
    auto& config_data           = config_instance->data();
    const auto& root_mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    spdlog::debug("mount_existing_subvols: opts:={}; ssd:={}; {}", fs_opts, is_ssd, root_mountpoint);

#ifdef NDEVENV
    gucc::utils::exec(fmt::format(FMT_COMPILE("btrfs subvolume list {} 2>/dev/null | cut -d' ' -f9 > /tmp/.subvols"), root_mountpoint), true);
    if (!gucc::utils::exec_checked(fmt::format(FMT_COMPILE("umount -v {} &>>/tmp/cachyos-install.log"), root_mountpoint))) {
        spdlog::error("Failed to unmount {}", root_mountpoint);
        return false;
    }

    const auto& subvol_list = gucc::utils::make_multiline(gucc::utils::exec("cat /tmp/.subvols"sv));

    // Get mountpoints of subvolumes
    std::vector<gucc::fs::BtrfsSubvolume> subvolumes{};
    for (auto&& subvol : subvol_list) {
        // Ask for mountpoint
        const auto& content = fmt::format(FMT_COMPILE("\nInput mountpoint of\nthe subvolume {}\nas it would appear\nin installed system\n(without prepending {}).\n"), subvol, root_mountpoint);
        std::string mountpoint{"/"};
        if (!tui::detail::inputbox_widget(mountpoint, content, size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
            return true;
        }
        mountpoint = std::string{gucc::utils::trim(mountpoint)};
        subvolumes.push_back(gucc::fs::BtrfsSubvolume{.subvolume = std::move(subvol), .mountpoint = std::move(mountpoint)});
    }

    // TODO(vnepogodin): add confirmation for selected mountpoint for particular subvolume

    // Mount subvolumes
    if (!gucc::fs::btrfs_mount_subvols(subvolumes, root_part->device, root_mountpoint, fs_opts)) {
        spdlog::error("Failed to mount btrfs subvolumes");
        return false;
    }

    if (!gucc::fs::btrfs_append_subvolumes(partitions, subvolumes)) {
        spdlog::error("Failed to append btrfs subvolumes into partition scheme");
        return false;
    }

    // need to find it again, due to modifying the parts
    root_part = find_root_btrfs_part(partitions);
#endif
    utils::dump_partition_to_log(*root_part);
    return true;
}

std::vector<std::string> lvm_show_vg() noexcept {
    const auto& vg_list = gucc::utils::make_multiline(gucc::utils::exec("lvs --noheadings | awk '{print $2}' | uniq"sv));

    std::vector<std::string> res{};
    res.reserve(vg_list.size());
    for (const auto& vg : vg_list) {
        const auto& temp = gucc::utils::exec(fmt::format(FMT_COMPILE("vgdisplay {} | grep -i \"vg size\" | {}"), vg, "awk '{print $3$4}'"sv));
        res.push_back(temp);
    }

    return res;
}

// Automated configuration of zfs. Creates a new zpool and a default set of filesystems
bool zfs_auto_pres(const std::string_view& partition, const std::string_view& zfs_zpool_name) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

#ifdef NDEVENV
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    auto result = cachyos::installer::zfs_auto_pres(partition, zfs_zpool_name, mountpoint);
    if (!result) {
        spdlog::error("{}", result.error());
        return false;
    }
    config_data["ZFS_SETUP_CONFIG"] = std::move(*result);
#else
    spdlog::info("[DRY-RUN] Would create ZFS automatically on {} with pool {}", partition, zfs_zpool_name);
#endif

    config_data["ZFS"] = 1;

    // insert zpool name into config
    auto& zfs_zpool_names = std::get<std::vector<std::string>>(config_data["ZFS_ZPOOL_NAMES"]);
    zfs_zpool_names.emplace_back(zfs_zpool_name);

    return true;
}

// creates a new zpool on an existing partition
bool zfs_create_zpool(const std::string_view& partition, const std::string_view& pool_name) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

#ifdef NDEVENV
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    auto result = cachyos::installer::zfs_create_zpool(partition, pool_name, mountpoint);
    if (!result) {
        spdlog::error("{}", result.error());
        return false;
    }
#else
    spdlog::info("[DRY-RUN] Would create zpool '{}' on '{}'", pool_name, partition);
#endif

    config_data["ZFS"] = 1;

    // insert zpool name into config
    auto& zfs_zpool_names = std::get<std::vector<std::string>>(config_data["ZFS_ZPOOL_NAMES"]);
    zfs_zpool_names.emplace_back(pool_name);

    return true;
}

// Other filesystems
void select_filesystem(std::string_view file_sys) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    config_data["FILESYSTEM_NAME"] = std::string{file_sys.data()};

    const auto fs_type = gucc::fs::string_to_filesystem_type(file_sys);
    if (fs_type == gucc::fs::FilesystemType::Unknown && file_sys != "zfs"sv) {
        spdlog::error("Invalid filesystem ('{}')!", file_sys);
        return;
    }

    // Get mkfs command
    const auto mkfs_cmd = gucc::fs::get_mkfs_command(fs_type);
    if (!mkfs_cmd.empty()) {
        config_data["FILESYSTEM"] = std::string{mkfs_cmd};
    }

    // Get available mount options
    const auto& mount_opts = utils::get_available_mount_opts(file_sys);
    config_data["fs_opts"] = mount_opts;

    // Load kernel modules for specific filesystems
#ifdef NDEVENV
    cachyos::installer::load_filesystem_module(file_sys);
#endif
}

auto get_available_mount_opts(std::string_view fstype) noexcept -> std::vector<std::string> {
    return cachyos::installer::get_available_mount_opts(fstype);
}

auto mount_partition(std::string_view partition, std::string_view mountpoint, std::string_view mount_dev, std::string_view mount_opts) noexcept -> bool {
    // Make the mount directory
    const auto& mount_dir = fmt::format(FMT_COMPILE("{}{}"), mountpoint, mount_dev);
#ifdef NDEVENV
    std::error_code err{};
    ::fs::create_directories(mount_dir, err);
    if (err) {
        spdlog::error("Failed to make directory {}: {}", mount_dev, err.message());
        return false;
    }
#endif

    // TODO(vnepogodin): use libmount instead.
    // see https://github.com/util-linux/util-linux/blob/master/sys-utils/mount.c#L734
#ifdef NDEVENV
    if (!gucc::mount::mount_partition(partition, mount_dir, mount_opts)) {
        spdlog::error("Failed to mount partition {} at {} with {}", partition, mount_dir, mount_opts);
        return false;
    }
#else
    spdlog::info("[DRY-RUN] Would mount {} at {} with options '{}'", partition, mount_dir, mount_opts);
#endif

    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    auto& luks_name       = std::get<std::string>(config_data["LUKS_NAME"]);
    auto& luks_dev        = std::get<std::string>(config_data["LUKS_DEV"]);
    auto& luks_uuid       = std::get<std::string>(config_data["LUKS_UUID"]);

    auto& is_luks = std::get<std::int32_t>(config_data["LUKS"]);
    auto& is_lvm  = std::get<std::int32_t>(config_data["LVM"]);

    // Use the actual mounted device path for querying LUKS/LVM, which might be a mapper device
    const auto& mounted_device = gucc::utils::exec(fmt::format("findmnt -n -o SOURCE {}", mount_dir));
    const auto& query_device   = mounted_device.empty() ? partition : mounted_device;
    if (!gucc::mount::query_partition(query_device, is_luks, is_lvm, luks_name, luks_dev, luks_uuid)) {
        spdlog::error("Failed to query partition: {} (queried as {})", partition, query_device);
        return false;
    }

    spdlog::debug("partition '{}': is_luks:={};is_lvm:={};luks_name:='{}';luks_dev:='{}';luks_uuid:='{}'", partition, is_luks, is_lvm, luks_name, luks_dev, luks_uuid);
    return true;
}

auto is_volume_removable() noexcept -> bool {
    const auto& mountpoint_info = utils::get_mountpoint();
    return cachyos::installer::is_volume_removable(mountpoint_info);
}

auto apply_swap_selection(const SwapSelection& swap, std::vector<gucc::fs::Partition>& partitions) noexcept -> bool {
    auto* config_instance       = Config::instance();
    auto& config_data           = config_instance->data();
    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& swapfile_path   = fmt::format(FMT_COMPILE("{}/swapfile"), mountpoint_info);

    switch (swap.type) {
    case SwapSelection::Type::Swapfile: {
#ifdef NDEVENV
        if (!gucc::swap::make_swapfile(mountpoint_info, swap.swapfile_size)) {
            spdlog::error("Failed to create swapfile: {}", swapfile_path);
            return false;
        }
#endif
        config_data["SWAP_DEVICE"] = swapfile_path;
        return true;
    }
    case SwapSelection::Type::Partition: {
        // TODO(vnepogodin): handle swap partition mount options?
        auto swap_mountopts = gucc::fs::get_default_mount_opts(gucc::fs::FilesystemType::LinuxSwap, false);
        auto swap_partition = gucc::fs::Partition{.fstype = "linuxswap"s, .mountpoint = ""s, .device = swap.device, .mount_opts = std::move(swap_mountopts)};

#ifdef NDEVENV
        if (!gucc::swap::make_swap_partition(swap_partition)) {
            spdlog::error("Failed to create swap partition");
            return false;
        }
#endif
        config_data["SWAP_DEVICE"] = swap.device;

        // TODO(vnepogodin): handle luks information
        const auto& part_uuid   = gucc::fs::utils::get_device_uuid(swap.device);
        swap_partition.uuid_str = part_uuid;
        // insert swap partition
        utils::dump_partition_to_log(swap_partition);
        partitions.emplace_back(std::move(swap_partition));
        return true;
    }
    case SwapSelection::Type::None:
        return true;
    }
    return true;
}

auto apply_esp_selection(const EspSelection& esp, std::vector<gucc::fs::Partition>& partitions, bool quiet) noexcept -> bool {
    if (esp.device.empty()) {
        return false;
    }

    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    config_data["UEFI_PART"]  = esp.device;
    config_data["UEFI_MOUNT"] = esp.mountpoint;

    auto esp_partition = utils::setup_esp_partition(esp.device, esp.mountpoint, esp.format_requested);
    if (!esp_partition) {
        spdlog::error("Failed to setup ESP partition");
        return false;
    }

    const auto& mountpoint_info = utils::get_mountpoint();
    const auto& part_mountpoint = fmt::format(FMT_COMPILE("{}{}"), mountpoint_info, esp.mountpoint);
    tui::confirm_mount(part_mountpoint, quiet);

    // insert boot partition
    partitions.emplace_back(std::move(*esp_partition));
    return true;
}

auto apply_root_partition_actions(const RootPartitionSelection& selection, const std::vector<gucc::fs::BtrfsSubvolume>& btrfs_subvols, std::vector<gucc::fs::Partition>& partition_schema) noexcept -> bool {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);

    // 1. Format the partition if requested (already approved in preview)
    if (selection.format_requested) {
#ifdef NDEVENV
        const auto& mkfs_cmd = fmt::format(FMT_COMPILE("{} {}"), selection.mkfs_command, selection.device);
        if (!gucc::utils::exec_checked(mkfs_cmd)) {
            spdlog::error("Failed to format partition {} with command: {}", selection.device, selection.mkfs_command);
            return false;
        }
        spdlog::info("Formatted {} with {}", selection.device, selection.mkfs_command);
#else
        spdlog::info("[DRY-RUN] Would format {} with command: {}", selection.device, selection.mkfs_command);
#endif
    }

    // 2. Mount the partition
    auto& luks_name = std::get<std::string>(config_data["LUKS_NAME"]);
    auto& luks_dev  = std::get<std::string>(config_data["LUKS_DEV"]);
    auto& luks_uuid = std::get<std::string>(config_data["LUKS_UUID"]);
    auto& is_luks   = std::get<std::int32_t>(config_data["LUKS"]);
    auto& is_lvm    = std::get<std::int32_t>(config_data["LVM"]);

    // Reset before query
    luks_name = "";
    luks_dev  = "";
    luks_uuid = "";
    is_luks   = 0;
    is_lvm    = 0;

    if (!utils::mount_partition(selection.device, mountpoint_info, "/"sv, selection.mount_opts)) {
        spdlog::error("Failed to mount root partition {}", selection.device);
        return false;
    }

    // 3. Add partition info to the schema
    const auto& mount_fstype = gucc::fs::utils::get_mountpoint_fs(mountpoint_info);
    const auto& part_fs      = mount_fstype.empty() ? (selection.fstype == "skip"sv ? "unknown"s : selection.fstype) : mount_fstype;
    auto root_part_struct    = gucc::fs::Partition{.fstype = part_fs, .mountpoint = "/"s, .device = selection.device, .mount_opts = selection.mount_opts};

    // Get UUID of made partition
    const auto& root_part_uuid = gucc::fs::utils::get_device_uuid(root_part_struct.device);
    root_part_struct.uuid_str  = root_part_uuid;

    // Get luks information about the partition
    if (is_luks && !luks_name.empty()) {
        root_part_struct.luks_mapper_name = luks_name;
    }
    // Store the UUID of the underlying *encrypted* partition if available
    if (is_luks && !luks_uuid.empty()) {
        root_part_struct.luks_uuid = luks_uuid;
    }

    utils::dump_partition_to_log(root_part_struct);

    // insert root partition
    partition_schema.emplace_back(std::move(root_part_struct));

    // 4. Handle BTRFS subvolumes
    return apply_btrfs_subvolumes(btrfs_subvols, selection, mountpoint_info, partition_schema);
}

auto convert_additional_selections(const std::vector<AdditionalPartSelection>& additional)
    -> std::vector<cachyos::installer::AdditionalPartSelection> {
    std::vector<cachyos::installer::AdditionalPartSelection> result;
    result.reserve(additional.size());
    for (const auto& p : additional) {
        result.push_back({
            .device           = p.device,
            .mountpoint       = p.mountpoint,
            .fstype           = p.fstype,
            .mkfs_command     = p.mkfs_command,
            .mount_opts       = p.mount_opts,
            .format_requested = p.format_requested,
        });
    }
    return result;
}

auto apply_mount_selections_interactive(const MountSelections& selections) noexcept -> bool {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    std::vector<gucc::fs::Partition> partitions{};

    // 1. Apply root partition (format + mount + btrfs subvols including existing detection)
    if (!apply_root_partition_actions(selections.root, selections.btrfs_subvolumes, partitions)) {
        spdlog::error("Failed to apply root partition actions for {}", selections.root.device);
        return false;
    }

    // 2. Apply swap
    if (!apply_swap_selection(selections.swap, partitions)) {
        spdlog::error("Failed to apply swap actions");
        return false;
    }

    // 3. Apply additional partitions
    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    for (const auto& p : selections.additional) {
        // Format if requested
        if (p.format_requested) {
#ifdef NDEVENV
            const auto& mkfs_cmd = fmt::format(FMT_COMPILE("{} {}"), p.mkfs_command, p.device);
            if (!gucc::utils::exec_checked(mkfs_cmd)) {
                spdlog::error("Failed to format {} with {}", p.device, p.mkfs_command);
                return false;
            }
            spdlog::info("Formatted {} with {}", p.device, p.mkfs_command);
#else
            spdlog::info("[DRY-RUN] Would format {} with {}", p.device, p.mkfs_command);
#endif
        }

        // Mount
        if (!utils::mount_partition(p.device, mountpoint_info, p.mountpoint, p.mount_opts)) {
            spdlog::error("Failed to mount {} at {}", p.device, p.mountpoint);
            return false;
        }

        // get mountpoint
        const auto& part_mountpoint = fmt::format(FMT_COMPILE("{}{}"), mountpoint_info, p.mountpoint);
        const auto& part_fs         = gucc::fs::utils::get_mountpoint_fs(part_mountpoint);
        auto part_struct            = gucc::fs::Partition{
            .fstype     = part_fs.empty() ? p.fstype : std::string{part_fs},
            .mountpoint = p.mountpoint,
            .device     = p.device,
            .mount_opts = p.mount_opts,
        };
        part_struct.uuid_str = gucc::fs::utils::get_device_uuid(p.device);

        // get luks information about the additional partition
        const auto& luks_name = std::get<std::string>(config_data["LUKS_NAME"]);
        const auto& luks_uuid = std::get<std::string>(config_data["LUKS_UUID"]);
        if (!luks_name.empty()) {
            part_struct.luks_mapper_name = luks_name;
        }
        if (!luks_uuid.empty()) {
            part_struct.luks_uuid = luks_uuid;
        }

        utils::dump_partition_to_log(part_struct);
        // insert partition
        partitions.emplace_back(std::move(part_struct));

        // Determine if a separate /boot is used.
        // 0 = no separate boot,
        // 1 = separate non-lvm boot,
        // 2 = separate lvm boot. For Grub configuration
        if (p.mountpoint == "/boot"sv) {
            config_data["LVM_SEP_BOOT"] = 1;
            const auto& boot_devices    = gucc::disk::list_block_devices();
            if (boot_devices) {
                const auto& boot_dev = gucc::disk::find_device_by_name(*boot_devices, p.device);
                if (boot_dev && boot_dev->type == "lvm") {
                    config_data["LVM_SEP_BOOT"] = 2;
                }
            }
        }
    }

    // 4. Apply ESP
    if (!apply_esp_selection(selections.esp, partitions)) {
        spdlog::error("Failed to apply ESP actions");
        return false;
    }

    // 5. Detect LUKS
    utils::get_cryptroot();
    utils::get_cryptboot();

    // 6. Dump and store partition schema globally
    utils::dump_partitions_to_log(partitions);
    config_data["PARTITION_SCHEMA"] = partitions;
    spdlog::info("Partition schema stored with {} entries", partitions.size());

    return true;
}

auto apply_mount_selections(const MountSelections& selections) noexcept -> bool {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    // Existing btrfs subvols require TUI interaction — handle locally.
    // Only applies when root is btrfs, not formatting, and no subvols were selected.
    const bool may_have_existing_subvols = selections.btrfs_subvolumes.empty()
        && !selections.root.format_requested
        && (selections.root.fstype == "btrfs"sv || selections.root.fstype == "skip"sv);
    if (may_have_existing_subvols) {
        return apply_mount_selections_interactive(selections);
    }

#ifdef NDEVENV
    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);

    // Convert TUI selections to library types
    const cachyos::installer::MountSelections lib_selections{
        .root = {
            .device           = selections.root.device,
            .fstype           = selections.root.fstype,
            .mkfs_command     = selections.root.mkfs_command,
            .mount_opts       = selections.root.mount_opts,
            .format_requested = selections.root.format_requested,
        },
        .swap = {
            .type          = static_cast<cachyos::installer::SwapSelection::Type>(selections.swap.type),
            .device        = selections.swap.device,
            .swapfile_size = selections.swap.swapfile_size,
            .needs_mkswap  = selections.swap.needs_mkswap,
        },
        .esp = {
            .device           = selections.esp.device,
            .mountpoint       = selections.esp.mountpoint,
            .format_requested = selections.esp.format_requested,
        },
        .additional       = convert_additional_selections(selections.additional),
        .btrfs_subvolumes = selections.btrfs_subvolumes,
    };

    auto result = cachyos::installer::apply_mount_selections(lib_selections, mountpoint_info);
    if (!result) {
        spdlog::error("{}", result.error());
        return false;
    }

    auto& [partitions, swap_device, lvm_sep_boot] = *result;

    // Store results in Config
    if (!swap_device.empty()) {
        config_data["SWAP_DEVICE"] = std::move(swap_device);
    }
    config_data["LVM_SEP_BOOT"] = lvm_sep_boot;
    if (!selections.esp.device.empty()) {
        config_data["UEFI_PART"]    = selections.esp.device;
        config_data["UEFI_MOUNT"]   = selections.esp.mountpoint;
        const auto& part_mountpoint = fmt::format(FMT_COMPILE("{}{}"), mountpoint_info, selections.esp.mountpoint);
        tui::confirm_mount(part_mountpoint, false);
    }

    // Detect LUKS
    utils::get_cryptroot();
    utils::get_cryptboot();
#else
    spdlog::info("[DRY-RUN] Would apply mount selections for root={}", selections.root.device);
    std::vector<gucc::fs::Partition> partitions{
        gucc::fs::Partition{.fstype = selections.root.fstype, .mountpoint = "/"s, .device = selections.root.device},
    };
#endif
    utils::dump_partitions_to_log(partitions);
    config_data["PARTITION_SCHEMA"] = std::move(partitions);

    return true;
}

}  // namespace utils
