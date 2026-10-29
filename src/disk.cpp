#include "disk.hpp"
#include "global_storage.hpp"
#include "misc.hpp"
#include "utils.hpp"
#include "widgets.hpp"

// import installer-lib
#include "cachyos/disk.hpp"

// import gucc
#include "gucc/btrfs.hpp"
#include "gucc/btrfs_query.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/lvm.hpp"
#include "gucc/mount_partitions.hpp"
#include "gucc/partition.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/ranges_utils.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/system_query.hpp"

#include <functional>  // for bind_back
#include <ranges>      // for ranges::*

#include <ftxui/component/component.hpp>           // for Renderer, Button
#include <ftxui/component/component_options.hpp>   // for ButtonOption
#include <ftxui/component/screen_interactive.hpp>  // for Component, ScreenI...
#include <ftxui/dom/elements.hpp>                  // for operator|, size
#include <sys/mount.h>                             // for umount

#include <fmt/compile.h>
#include <fmt/core.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace {

// TODO(vnepogodin): refactor that out of this file
constexpr auto find_root_btrfs_part(std::vector<gucc::fs::Partition>& parts) noexcept -> gucc::fs::Partition* {
    return gucc::utils::find_if_ptr(parts, std::bind_back(gucc::fs::is_root_on, "btrfs"sv));
}

#ifdef NDEVENV
void store_luks_config(const cachyos::installer::MountPartitionResult& mount_result) noexcept {
    config::get<std::int32_t>("LUKS")     = mount_result.is_luks;
    config::get<std::int32_t>("LVM")      = mount_result.is_lvm;
    config::get<std::string>("LUKS_NAME") = mount_result.luks_name;
    config::get<std::string>("LUKS_DEV")  = mount_result.luks_dev;
    config::get<std::string>("LUKS_UUID") = mount_result.luks_uuid;
}
#endif

void register_zpool(std::string_view pool_name) noexcept {
    config::get<std::int32_t>("ZFS") = 1;
    config::get<std::vector<std::string>>("ZFS_ZPOOL_NAMES").emplace_back(pool_name);
}

}  // namespace

namespace utils {

auto default_btrfs_subvolumes() noexcept -> std::vector<gucc::fs::BtrfsSubvolume> {
    return cachyos::installer::default_btrfs_subvolumes();
}

auto select_btrfs_subvolumes(std::string_view root_mountpoint) noexcept -> std::vector<gucc::fs::BtrfsSubvolume> {
    const std::vector<std::string> menu_entries = {
        "automatic",
        "manual",
    };
    static constexpr auto btrfsvols_body = "\nAutomatic mode\nis designed to allow integration\nwith snapper, non-recursive snapshots,\nseparating system and user data and\nrestoring snapshots without losing data.\n"sv;
    const auto mode                      = tui::detail::select_from_menu(menu_entries, btrfsvols_body);
    if (!mode) {
        return {};
    }
    if (*mode == 0) {
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
    if (root_part == nullptr) {
        spdlog::error("mount_existing_subvols: unable to find btrfs root part");
        return false;
    }

    const auto& fs_opts        = utils::default_mount_opts(root_part->device, "btrfs"sv);
    const auto root_mountpoint = utils::get_mountpoint();
    spdlog::debug("mount_existing_subvols: opts:={}; {}", fs_opts, root_mountpoint);

#ifdef NDEVENV
    // List existing subvolumes while the filesystem is still mounted, then unmount.
    const auto& exist_subvols = gucc::fs::list_btrfs_subvolumes(root_mountpoint, ""sv);
    if (auto res = gucc::mount::umount(root_mountpoint); !res) {
        spdlog::error("Failed to unmount {}: {}", root_mountpoint, gucc::to_string(res.error()));
        return false;
    }

    // Get mountpoints of subvolumes
    std::vector<gucc::fs::BtrfsSubvolume> subvolumes{};
    for (const auto& exist_subvol : exist_subvols) {
        // Ask for mountpoint
        const auto& content = fmt::format(FMT_COMPILE("\nInput mountpoint of\nthe subvolume {}\nas it would appear\nin installed system\n(without prepending {}).\n"), exist_subvol.subvolume, root_mountpoint);
        std::string mountpoint{"/"};
        if (!tui::detail::inputbox_widget(mountpoint, content, size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
            return true;
        }
        mountpoint = std::string{gucc::utils::trim(mountpoint)};
        subvolumes.push_back(gucc::fs::BtrfsSubvolume{.subvolume = exist_subvol.subvolume, .mountpoint = std::move(mountpoint)});
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

auto lvm_show_vg() noexcept -> std::vector<std::pair<std::string, std::string>> {
#ifdef NDEVENV
    return gucc::lvm::show_volume_groups();
#else
    spdlog::info("[DRY-RUN] Would query volume groups");
    return {};
#endif
}

// Automated configuration of zfs. Creates a new zpool and a default set of filesystems
bool zfs_auto_pres(const std::string_view& partition, const std::string_view& zfs_zpool_name) noexcept {
#ifdef NDEVENV
    auto result = cachyos::installer::zfs_auto_pres(partition, zfs_zpool_name, utils::get_mountpoint());
    if (!result) {
        spdlog::error("{}", result.error());
        return false;
    }
    config::get<gucc::fs::ZfsSetupConfig>("ZFS_SETUP_CONFIG") = std::move(*result);
#else
    spdlog::info("[DRY-RUN] Would create ZFS automatically on {} with pool {}", partition, zfs_zpool_name);
#endif
    register_zpool(zfs_zpool_name);
    return true;
}

// creates a new zpool on an existing partition
bool zfs_create_zpool(const std::string_view& partition, const std::string_view& pool_name) noexcept {
#ifdef NDEVENV
    if (auto result = cachyos::installer::zfs_create_zpool(partition, pool_name, utils::get_mountpoint()); !result) {
        spdlog::error("{}", result.error());
        return false;
    }
#else
    spdlog::info("[DRY-RUN] Would create zpool '{}' on '{}'", pool_name, partition);
#endif
    register_zpool(pool_name);
    return true;
}

// Other filesystems
void select_filesystem(std::string_view file_sys) noexcept {
    config::get<std::string>("FILESYSTEM_NAME") = std::string{file_sys};

    const auto fs_type = gucc::fs::string_to_filesystem_type(file_sys);
    if (fs_type == gucc::fs::FilesystemType::Unknown && file_sys != "zfs"sv) {
        spdlog::error("Invalid filesystem ('{}')!", file_sys);
        return;
    }

    // Get mkfs command
    const auto mkfs_cmd = gucc::fs::get_mkfs_command(fs_type);
    if (!mkfs_cmd.empty()) {
        config::get<std::string>("FILESYSTEM") = std::string{mkfs_cmd};
    }

    config::get<std::vector<std::string>>("fs_opts") = utils::get_available_mount_opts(file_sys);

    // Load kernel modules for specific filesystems
#ifdef NDEVENV
    cachyos::installer::load_filesystem_module(file_sys);
#endif
}

auto get_available_mount_opts(std::string_view fstype) noexcept -> std::vector<std::string> {
    return cachyos::installer::get_available_mount_opts(fstype);
}

auto default_mount_opts(std::string_view device, std::string_view fstype) noexcept -> std::string {
    return gucc::fs::get_default_mount_opts(gucc::fs::string_to_filesystem_type(fstype), gucc::disk::is_device_ssd(device));
}

auto mount_partition(std::string_view partition, std::string_view mountpoint, std::string_view mount_dev, std::string_view mount_opts) noexcept -> bool {
#ifdef NDEVENV
    auto result = cachyos::installer::mount_partition(partition, mountpoint, mount_dev, mount_opts);
    if (!result) {
        spdlog::error("{}", result.error());
        return false;
    }
    store_luks_config(*result);
#else
    spdlog::info("[DRY-RUN] Would mount {} at {}{} with options '{}'", partition, mountpoint, mount_dev, mount_opts);
#endif
    return true;
}

auto is_volume_removable() noexcept -> bool {
    const auto& mountpoint_info = utils::get_mountpoint();
    return cachyos::installer::is_volume_removable(mountpoint_info);
}

auto apply_swap_selection(const SwapSelection& swap, std::vector<gucc::fs::Partition>& partitions) noexcept -> bool {
    const auto mountpoint_info = utils::get_mountpoint();
    auto& swap_device          = config::get<std::string>("SWAP_DEVICE");

#ifdef NDEVENV
    auto result = cachyos::installer::apply_swap(swap, mountpoint_info);
    if (!result) {
        spdlog::error("{}", result.error());
        return false;
    }
    if (!result->swap_device.empty()) {
        swap_device = std::move(result->swap_device);
    }
    if (result->swap_partition) {
        utils::dump_partition_to_log(*result->swap_partition);
        partitions.emplace_back(std::move(*result->swap_partition));
    }
#else
    switch (swap.type) {
    case SwapSelection::Type::Swapfile:
        spdlog::info("[DRY-RUN] Would create swapfile at {}/swapfile (size: {})", mountpoint_info, swap.swapfile_size);
        swap_device = gucc::utils::target_path(mountpoint_info, "/swapfile"sv);
        break;
    case SwapSelection::Type::Partition:
        spdlog::info("[DRY-RUN] Would create swap partition on {}", swap.device);
        swap_device = swap.device;
        partitions.emplace_back(gucc::fs::Partition{.fstype = "linuxswap"s, .mountpoint = ""s, .device = swap.device});
        break;
    case SwapSelection::Type::None:
        break;
    }
#endif
    return true;
}

auto apply_esp_selection(const EspSelection& esp, std::vector<gucc::fs::Partition>& partitions, bool quiet) noexcept -> bool {
    if (esp.device.empty()) {
        return false;
    }

    config::get<std::string>("UEFI_PART")  = esp.device;
    config::get<std::string>("UEFI_MOUNT") = esp.mountpoint;

    auto esp_partition = utils::setup_esp_partition(esp.device, esp.mountpoint, esp.format_requested);
    if (!esp_partition) {
        spdlog::error("Failed to setup ESP partition");
        return false;
    }

    const auto& mountpoint_info = utils::get_mountpoint();
    const auto& part_mountpoint = gucc::utils::target_path(mountpoint_info, esp.mountpoint);
    tui::confirm_mount(part_mountpoint, quiet);

    // insert boot partition
    partitions.emplace_back(std::move(*esp_partition));
    return true;
}

auto apply_root_partition_actions(const RootPartitionSelection& selection, const std::vector<gucc::fs::BtrfsSubvolume>& btrfs_subvols, std::vector<gucc::fs::Partition>& partition_schema) noexcept -> bool {
#ifdef NDEVENV
    const auto mountpoint_info = utils::get_mountpoint();
    auto result                = cachyos::installer::apply_root_partition(selection, btrfs_subvols, mountpoint_info);
    if (!result) {
        spdlog::error("{}", result.error());
        return false;
    }

    store_luks_config(result->mount_info);

    partition_schema = std::move(result->partitions);
    utils::dump_partitions_to_log(partition_schema);

    // Handle existing btrfs subvolume detection (requires TUI interaction)
    if (btrfs_subvols.empty() && !selection.format_requested) {
        const auto& exist_subvols = gucc::fs::list_btrfs_subvolumes(mountpoint_info, ""sv);
        if (!exist_subvols.empty()) {
            spdlog::info("Found existing btrfs subvolumes, mounting them automatically");
            if (!utils::mount_existing_subvols(partition_schema)) {
                spdlog::error("Failed to mount existing btrfs subvolumes");
                return false;
            }
        }
    }
#else
    spdlog::info("[DRY-RUN] Would apply root partition actions for {}", selection.device);
    auto root_part = gucc::fs::Partition{
        .fstype     = selection.fstype,
        .mountpoint = "/"s,
        .device     = selection.device,
        .mount_opts = selection.mount_opts,
    };
    utils::dump_partition_to_log(root_part);
    partition_schema.emplace_back(std::move(root_part));

    if (!btrfs_subvols.empty()) {
        if (!gucc::fs::btrfs_append_subvolumes(partition_schema, btrfs_subvols)) {
            spdlog::error("Failed to append btrfs subvolumes into partition scheme");
            return false;
        }
    }
#endif
    return true;
}

auto apply_mount_selections_interactive(const MountSelections& selections) noexcept -> bool {
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
    const auto mountpoint_info = utils::get_mountpoint();
    auto& lvm_sep_boot         = config::get<std::int32_t>("LVM_SEP_BOOT");
#ifdef NDEVENV
    {
        auto additional_res = cachyos::installer::apply_additional_partitions(selections.additional, mountpoint_info, partitions);
        if (!additional_res) {
            spdlog::error("{}", additional_res.error());
            return false;
        }
        lvm_sep_boot = *additional_res;
    }
#else
    for (const auto& p : selections.additional) {
        if (p.format_requested) {
            spdlog::info("[DRY-RUN] Would format {} with {}", p.device, p.mkfs_command);
        }
        spdlog::info("[DRY-RUN] Would mount {} at {}{}", p.device, mountpoint_info, p.mountpoint);
        auto part_struct = gucc::fs::Partition{
            .fstype     = p.fstype,
            .mountpoint = p.mountpoint,
            .device     = p.device,
            .mount_opts = p.mount_opts,
        };
        utils::dump_partition_to_log(part_struct);
        partitions.emplace_back(std::move(part_struct));
        if (p.mountpoint == "/boot"sv) {
            lvm_sep_boot = 1;
        }
    }
#endif

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
    spdlog::info("Partition schema stored with {} entries", partitions.size());
    config::get<std::vector<gucc::fs::Partition>>("PARTITION_SCHEMA") = std::move(partitions);

    return true;
}

auto apply_mount_selections(const MountSelections& selections) noexcept -> bool {
    // Existing btrfs subvols require TUI interaction — handle locally.
    // Only applies when root is btrfs, not formatting, and no subvols were selected.
    const bool may_have_existing_subvols = selections.btrfs_subvolumes.empty()
        && !selections.root.format_requested
        && (selections.root.fstype == "btrfs"sv || selections.root.fstype == "skip"sv);
    if (may_have_existing_subvols) {
        return apply_mount_selections_interactive(selections);
    }

#ifdef NDEVENV
    auto& config_data          = Config::instance()->data();
    const auto mountpoint_info = utils::get_mountpoint();

    auto result = cachyos::installer::apply_mount_selections(selections, mountpoint_info);
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
        const auto& part_mountpoint = gucc::utils::target_path(mountpoint_info, selections.esp.mountpoint);
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
    config::get<std::vector<gucc::fs::Partition>>("PARTITION_SCHEMA") = std::move(partitions);
    return true;
}

auto mount_partition_headless() noexcept -> bool {
    const auto& partition = config::get<std::string>("PARTITION");
    const auto& mount_dev = config::get<std::string>("MOUNT");
    auto& mount_opts      = config::get<std::string>("MOUNT_OPTS");
    mount_opts            = utils::default_mount_opts(partition, config::get<std::string>("FILESYSTEM_NAME"));

    if (!utils::mount_partition(partition, utils::get_mountpoint(), mount_dev, mount_opts)) {
        spdlog::error("Failed to mount current partition {}", partition);
        return false;
    }

    // Remove from partition list
    auto& partitions        = config::get<std::vector<std::string>>("PARTITIONS");
    auto& number_partitions = config::get<std::int32_t>("NUMBER_PARTITIONS");
    std::erase_if(partitions, [&partition](std::string_view x) { return gucc::utils::contains(x, partition); });
    number_partitions -= 1;

    return true;
}

void make_esp_headless(std::vector<gucc::fs::Partition>& partitions,
    std::string_view part_name,
    gucc::bootloader::BootloaderType bootloader_type,
    bool reformat_part,
    std::string_view boot_part_mountpoint) noexcept {
    const auto& sys_info = config::get<std::string>("SYSTEM");

    /* clang-format off */
    if (sys_info != "UEFI"sv) { return; }
    /* clang-format on */

    auto& partition = config::get<std::string>("PARTITION");
    partition       = std::string{part_name};

    const auto& uefi_mount = boot_part_mountpoint.empty()
        ? utils::bootloader_default_mount(bootloader_type, sys_info)
        : boot_part_mountpoint;

    // If it is already a fat/vfat partition, skip formatting
    const bool is_fat_part = gucc::fs::utils::get_device_fstype(partition) == "vfat"sv;
    const bool do_format   = !is_fat_part && reformat_part;

    // Apply ESP
    utils::EspSelection esp_selection{
        .device           = partition,
        .mountpoint       = std::string{uefi_mount},
        .format_requested = do_format,
    };
    if (!utils::apply_esp_selection(esp_selection, partitions, true)) {
        spdlog::error("Failed to apply ESP actions");
    }
}

auto attach_luks_info(gucc::fs::Partition partition) noexcept -> gucc::fs::Partition {
    if (const auto& luks_name = config::get<std::string>("LUKS_NAME"); !luks_name.empty()) {
        partition.luks_mapper_name = luks_name;
    }
    if (const auto& luks_uuid = config::get<std::string>("LUKS_UUID"); !luks_uuid.empty()) {
        partition.luks_uuid = luks_uuid;
    }
    return partition;
}

}  // namespace utils
