#include "cachyos/disk.hpp"
#include "cachyos/packages.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/btrfs.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/fstab.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/mount_partitions.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/partitioning.hpp"
#include "gucc/subprocess.hpp"
#include "gucc/swap.hpp"
#include "gucc/system_query.hpp"
#include "gucc/umount_partitions.hpp"
#include "gucc/zfs.hpp"

#include <algorithm>    // for transform
#include <expected>     // for unexpected
#include <filesystem>   // for create_directories
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace {

/// Default ZFS pool creation options shared by all ZFS setup paths.
constexpr auto kDefaultZpoolOptions{"-f -o ashift=12 -o autotrim=on -O mountpoint=none -O acltype=posixacl -O atime=off -O relatime=off -O xattr=sa -O normalization=formD -O dnodesize=auto"sv};

/// Get appropriate default mountpoint for bootloader.
constexpr auto bootloader_default_mount(gucc::bootloader::BootloaderType bootloader, std::string_view system_mode) noexcept -> std::string_view {
    using gucc::bootloader::BootloaderType;
    if (bootloader == BootloaderType::SystemdBoot || bootloader == BootloaderType::Limine || system_mode == "BIOS"sv) {
        return "/boot"sv;
    } else if (bootloader == BootloaderType::Grub || bootloader == BootloaderType::Refind) {
        return "/boot/efi"sv;
    }
    return "unknown bootloader"sv;
}

/// Queries LUKS/LVM state for a partition and populates the Partition struct.
void query_partition_luks(std::string_view device, gucc::fs::Partition& part) noexcept {
    std::int32_t is_luks{};
    std::int32_t is_lvm{};
    std::string luks_name;
    std::string luks_dev;
    std::string luks_uuid;

    // TODO(vnepogodin): should check query res?
    gucc::mount::query_partition(device, is_luks, is_lvm, luks_name, luks_dev, luks_uuid);

    if (is_luks && !luks_name.empty()) {
        part.luks_mapper_name = luks_name;
    }
    if (is_luks && !luks_uuid.empty()) {
        part.luks_uuid = luks_uuid;
    }
}

}  // namespace

namespace cachyos::installer {

auto default_btrfs_subvolumes() noexcept -> std::vector<gucc::fs::BtrfsSubvolume> {
    return {
        gucc::fs::BtrfsSubvolume{.subvolume = "/@"s, .mountpoint = "/"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@home"s, .mountpoint = "/home"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@root"s, .mountpoint = "/root"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@srv"s, .mountpoint = "/srv"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@cache"s, .mountpoint = "/var/cache"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@tmp"s, .mountpoint = "/var/tmp"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@log"s, .mountpoint = "/var/log"s},
        // gucc::fs::BtrfsSubvolume{.subvolume = "/@snapshots"sv, .mountpoint = "/.snapshots"sv},
    };
}

auto get_available_mount_opts(std::string_view fstype) noexcept -> std::vector<std::string> {
    const auto& fs_type = gucc::fs::string_to_filesystem_type(fstype);
    const auto& fs_opts = gucc::fs::get_available_mount_opts(fs_type);

    std::vector<std::string> result{};
    result.reserve(fs_opts.size());
    std::ranges::transform(fs_opts, std::back_inserter(result),
        [](auto&& mount_opt) -> std::string { return mount_opt.name; });
    return result;
}

auto is_volume_removable(std::string_view mountpoint) noexcept -> bool {
    // Find the block device mounted at the given mountpoint
    const auto& devices = gucc::disk::list_block_devices();
    if (!devices) {
        spdlog::error("Failed to list block devices for removable check");
        return false;
    }

    const auto& mounted_dev = gucc::disk::find_device_by_mountpoint(*devices, mountpoint);
    if (!mounted_dev) {
        spdlog::error("No block device found at mountpoint {}", mountpoint);
        return false;
    }

    // Walk the pkname chain (handles mapper → crypt → part → disk) to find the parent disk
    const auto& disk_dev = gucc::disk::find_ancestor_of_type(*devices, mounted_dev->name, "disk"sv);
    if (!disk_dev) {
        spdlog::error("Could not find parent disk for {}", mounted_dev->name);
        return false;
    }

    // Query disk info which includes the sysfs removable flag
    const auto& disk_info = gucc::disk::get_disk_info(disk_dev->name);
    if (!disk_info) {
        spdlog::error("Failed to get disk info for {}", disk_dev->name);
        return false;
    }

    spdlog::info("root_device: {}. is_removable: {}", disk_info->device, disk_info->is_removable);
    return disk_info->is_removable;
}

auto auto_partition(std::string_view device, std::string_view system_mode,
    gucc::bootloader::BootloaderType bootloader, const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<std::vector<gucc::fs::Partition>, std::string> {
    spdlog::info("Running automatic partitioning");

    const auto& boot_mountpoint = bootloader_default_mount(bootloader, system_mode);
    const bool is_system_efi    = (system_mode == "UEFI"sv);

    auto partitions = gucc::disk::generate_default_partition_schema(device, boot_mountpoint, is_system_efi);
    if (partitions.empty()) {
        return std::unexpected("failed to generate default partition schema: it cannot be empty");
    }

    if (!gucc::disk::make_clean_partschema(device, partitions, is_system_efi)) {
        return std::unexpected("failed to perform automatic partitioning");
    }

    return partitions;
}

auto secure_wipe(std::string_view device, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    // Ensure wipe tool is installed
    auto needed_result = install_needed("wipe", child);
    if (!needed_result) {
        return std::unexpected(needed_result.error());
    }

    const auto& cmd = fmt::format(FMT_COMPILE("wipe -Ifre {}"), device);
    if (!gucc::utils::exec_follow({"/bin/sh", "-c", cmd}, child)) {
        return std::unexpected(fmt::format("failed to wipe device: {}", device));
    }
    return {};
}

auto zfs_auto_pres(std::string_view partition,
    std::string_view zpool_name, std::string_view /*mountpoint*/) noexcept
    -> std::expected<gucc::fs::ZfsSetupConfig, std::string> {

    const std::vector<gucc::fs::ZfsDataset> default_zfs_datasets{
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT"), zpool_name), .mountpoint = "none"s},
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT/cos"), zpool_name), .mountpoint = "none"s},
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT/cos/root"), zpool_name), .mountpoint = "/"s},
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT/cos/home"), zpool_name), .mountpoint = "/home"s},
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT/cos/varcache"), zpool_name), .mountpoint = "/var/cache"s},
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT/cos/varlog"), zpool_name), .mountpoint = "/var/log"s},
    };

    // passphrase should be known at this time, e.g. passing as arg to zfs_auto_pres func
    gucc::fs::ZfsSetupConfig zfs_setup_config{
        .zpool_name    = std::string(zpool_name),
        .zpool_options = std::string(kDefaultZpoolOptions),
        .passphrase    = std::nullopt,
        .datasets      = default_zfs_datasets,
    };

    if (!gucc::fs::zfs_create_with_config(partition, zfs_setup_config)) {
        return std::unexpected("failed to create ZFS automatically");
    }

    return zfs_setup_config;
}

auto zfs_create_zpool(std::string_view partition,
    std::string_view pool_name, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {

    std::string device_path{partition};

    // Find the PARTUUID of the partition
    const auto& blk_devices = gucc::disk::list_block_devices();
    if (blk_devices) {
        const auto& dev = gucc::disk::find_device_by_name(*blk_devices, partition);
        if (dev && dev->partuuid && !dev->partuuid->empty()) {
            device_path = *dev->partuuid;
        }
    }

    if (!gucc::fs::zfs_create_zpool(device_path, pool_name, kDefaultZpoolOptions)) {
        return std::unexpected("failed to create zpool");
    }

    // Since zfs manages mountpoints, export and re-import with root at mountpoint
    gucc::utils::exec(fmt::format(FMT_COMPILE("zpool export {} 2>>/tmp/cachyos-install.log"), pool_name), true);
    gucc::utils::exec(fmt::format(FMT_COMPILE("zpool import -R {} {} 2>>/tmp/cachyos-install.log"), mountpoint, pool_name), true);

    return {};
}

auto apply_mount_selections(const MountSelections& selections,
    std::string_view mountpoint) noexcept
    -> std::expected<MountApplicationResult, std::string> {
    spdlog::info("Applying mount selections on {}", mountpoint);
    MountApplicationResult result{};

    // 1. Format root partition if requested
    if (selections.root.format_requested) {
        const auto& mkfs_cmd = fmt::format(FMT_COMPILE("{} {}"), selections.root.mkfs_command, selections.root.device);
        if (!gucc::utils::exec_checked(mkfs_cmd)) {
            return std::unexpected(fmt::format("failed to format root partition {} with {}", selections.root.device, selections.root.mkfs_command));
        }
        spdlog::info("Formatted {} with {}", selections.root.device, selections.root.mkfs_command);
    }

    // 2. Mount root partition
    {
        std::error_code ec{};
        std::filesystem::create_directories(std::string{mountpoint}, ec);
        if (ec) {
            return std::unexpected(fmt::format("failed to create root mount directory {}: {}", mountpoint, ec.message()));
        }
        if (!gucc::mount::mount_partition(selections.root.device, mountpoint, selections.root.mount_opts)) {
            return std::unexpected(fmt::format("failed to mount root partition {} at {}", selections.root.device, mountpoint));
        }
    }

    // 3. Build root partition struct
    {
        const auto& mount_fstype = gucc::fs::utils::get_mountpoint_fs(mountpoint);
        std::string root_fs;
        if (!mount_fstype.empty()) {
            root_fs = mount_fstype;
        } else if (selections.root.fstype == "skip"sv) {
            root_fs = "unknown"s;
        } else {
            root_fs = selections.root.fstype;
        }

        auto root_part = gucc::fs::Partition{
            .fstype     = std::move(root_fs),
            .mountpoint = "/"s,
            .device     = selections.root.device,
            .mount_opts = selections.root.mount_opts,
        };
        root_part.uuid_str = gucc::fs::utils::get_device_uuid(selections.root.device);
        query_partition_luks(selections.root.device, root_part);
        result.partitions.emplace_back(std::move(root_part));
    }

    // 4. BTRFS subvolumes
    if (!selections.btrfs_subvolumes.empty()) {
        auto btrfs_res = apply_btrfs_subvolumes(selections.btrfs_subvolumes,
            selections.root, mountpoint, result.partitions);
        if (!btrfs_res) {
            return std::unexpected(btrfs_res.error());
        }
    }

    // 5. Swap
    switch (selections.swap.type) {
    case SwapSelection::Type::Swapfile: {
        const auto& swapfile_path = fmt::format(FMT_COMPILE("{}/swapfile"), mountpoint);
        if (!gucc::swap::make_swapfile(mountpoint, selections.swap.swapfile_size)) {
            return std::unexpected("failed to create swapfile");
        }
        result.swap_device = swapfile_path;
        break;
    }
    case SwapSelection::Type::Partition: {
        auto swap_opts = gucc::fs::get_default_mount_opts(gucc::fs::FilesystemType::LinuxSwap, false);
        auto swap_part = gucc::fs::Partition{
            .fstype     = "linuxswap"s,
            .mountpoint = ""s,
            .device     = selections.swap.device,
            .mount_opts = std::move(swap_opts),
        };
        if (!gucc::swap::make_swap_partition(swap_part)) {
            return std::unexpected("failed to create swap partition");
        }
        result.swap_device = selections.swap.device;
        swap_part.uuid_str = gucc::fs::utils::get_device_uuid(selections.swap.device);
        result.partitions.emplace_back(std::move(swap_part));
        break;
    }
    case SwapSelection::Type::None:
        break;
    }

    // 6. Additional partitions
    for (const auto& p : selections.additional) {
        if (p.format_requested) {
            const auto& mkfs_cmd = fmt::format(FMT_COMPILE("{} {}"), p.mkfs_command, p.device);
            if (!gucc::utils::exec_checked(mkfs_cmd)) {
                return std::unexpected(fmt::format("failed to format {} with {}", p.device, p.mkfs_command));
            }
            spdlog::info("Formatted {} with {}", p.device, p.mkfs_command);
        }

        const auto& mount_dir = fmt::format(FMT_COMPILE("{}{}"), mountpoint, p.mountpoint);
        {
            std::error_code ec{};
            std::filesystem::create_directories(mount_dir, ec);
            if (ec) {
                return std::unexpected(fmt::format("failed to create directory {}: {}", mount_dir, ec.message()));
            }
        }
        if (!gucc::mount::mount_partition(p.device, mount_dir, p.mount_opts)) {
            return std::unexpected(fmt::format("failed to mount {} at {}", p.device, p.mountpoint));
        }

        const auto& part_fs = gucc::fs::utils::get_mountpoint_fs(mount_dir);
        auto part           = gucc::fs::Partition{
            .fstype     = part_fs.empty() ? p.fstype : std::string{part_fs},
            .mountpoint = p.mountpoint,
            .device     = p.device,
            .mount_opts = p.mount_opts,
        };
        part.uuid_str = gucc::fs::utils::get_device_uuid(p.device);
        query_partition_luks(p.device, part);
        result.partitions.emplace_back(std::move(part));

        // Detect separate /boot for Grub configuration
        if (p.mountpoint == "/boot"sv) {
            result.lvm_sep_boot      = 1;
            const auto& boot_devices = gucc::disk::list_block_devices();
            if (boot_devices) {
                const auto& boot_dev = gucc::disk::find_device_by_name(*boot_devices, p.device);
                if (boot_dev && boot_dev->type == "lvm") {
                    result.lvm_sep_boot = 2;
                }
            }
        }
    }

    // 7. ESP
    if (!selections.esp.device.empty()) {
        auto esp_result = setup_esp_partition(selections.esp.device, selections.esp.mountpoint,
            mountpoint, selections.esp.format_requested);
        if (!esp_result) {
            return std::unexpected(esp_result.error());
        }
        result.partitions.emplace_back(std::move(*esp_result));
    }

    spdlog::info("Mount selections applied: {} partitions", result.partitions.size());
    return result;
}

auto apply_btrfs_subvolumes(const std::vector<gucc::fs::BtrfsSubvolume>& subvols, const RootPartitionSelection& selection,
    std::string_view mountpoint, std::vector<gucc::fs::Partition>& partitions) noexcept
    -> std::expected<void, std::string> {
    if (subvols.empty() && !selection.format_requested) {
        // If not formatting, existing subvolumes should be handled by the caller
        spdlog::info("not formatting btrfs on {}", mountpoint);
        return {};
    }
    /* clang-format off */
    if (subvols.empty()) { return {}; }
    /* clang-format on */

    // Create subvolumes (mount opts are already known from the selection)
    if (!gucc::fs::btrfs_create_subvols(subvols, selection.device, mountpoint, selection.mount_opts)) {
        return std::unexpected("failed to create btrfs subvolumes");
    }

    if (!gucc::fs::btrfs_append_subvolumes(partitions, subvols)) {
        return std::unexpected("failed to append btrfs subvolumes into partition scheme");
    }

    return {};
}

auto setup_esp_partition(std::string_view device, std::string_view mountpoint,
    std::string_view root_mountpoint, bool format_requested) noexcept
    -> std::expected<gucc::fs::Partition, std::string> {
    const bool is_boot_ssd = gucc::disk::is_device_ssd(device);
    auto boot_part_struct  = gucc::mount::setup_esp_partition(device, mountpoint, root_mountpoint, format_requested, is_boot_ssd);
    if (!boot_part_struct) {
        return std::unexpected(fmt::format("failed to setup ESP partition {}", device));
    }
    return std::move(*boot_part_struct);
}

auto umount_partitions(std::string_view mountpoint,
    const std::vector<std::string>& zfs_pools,
    std::string_view swap_device) noexcept
    -> std::expected<void, std::string> {
    // Disable swap if it was created
    const auto& swapoff_cmd = fmt::format(FMT_COMPILE("swapoff {}"), swap_device);
    if (!swap_device.empty() && !gucc::utils::exec_checked(swapoff_cmd)) {
        spdlog::error("Failed to disable swap on {}", swap_device);
    }

    // Unmount all detected partitions on mountpoint
    if (!gucc::umount::umount_partitions(mountpoint, zfs_pools)) {
        return std::unexpected("failed to umount partitions");
    }
    return {};
}

auto generate_fstab(std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    spdlog::info("Generating fstab on {}", mountpoint);
    if (!gucc::fs::run_genfstab_on_mount(mountpoint)) {
        return std::unexpected("failed to generate fstab");
    }
    return {};
}

void load_filesystem_module(std::string_view fstype) noexcept {
    const auto fs_type = gucc::fs::string_to_filesystem_type(fstype);
    if (fs_type == gucc::fs::FilesystemType::Btrfs) {
        if (!gucc::utils::exec_checked("modprobe btrfs"sv)) {
            spdlog::warn("failed to load btrfs kernel module");
        }
    } else if (fs_type == gucc::fs::FilesystemType::F2fs) {
        if (!gucc::utils::exec_checked("modprobe f2fs"sv)) {
            spdlog::warn("failed to load f2fs kernel module");
        }
    }
}

}  // namespace cachyos::installer
