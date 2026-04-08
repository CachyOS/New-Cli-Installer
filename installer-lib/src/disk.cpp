#include "cachyos/disk.hpp"
#include "cachyos/packages.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/btrfs.hpp"
#include "gucc/crypto_detection.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/fstab.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/mount_partitions.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/partitioning.hpp"
#include "gucc/string_utils.hpp"
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

/// Transfer LUKS info from mount result into a Partition struct.
void apply_luks_info(const cachyos::installer::MountPartitionResult& mount_res, gucc::fs::Partition& part) noexcept {
    if (mount_res.is_luks && !mount_res.luks_name.empty()) {
        part.luks_mapper_name = mount_res.luks_name;
    }
    if (mount_res.is_luks && !mount_res.luks_uuid.empty()) {
        part.luks_uuid = mount_res.luks_uuid;
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

auto bootloader_esp_layout(gucc::bootloader::BootloaderType bootloader,
    std::string_view system_mode) noexcept -> EspLayout {
    using gucc::bootloader::BootloaderType;
    std::string_view size{"2GiB"sv};
    if (bootloader == BootloaderType::Grub) {
        size = "512MiB"sv;
    } else if (bootloader == BootloaderType::Limine) {
        size = "4GiB"sv;
    }
    return EspLayout{
        .mountpoint = std::string{bootloader_default_mount(bootloader, system_mode)},
        .size       = std::string{size},
    };
}

auto auto_partition(std::string_view device, std::string_view system_mode,
    gucc::bootloader::BootloaderType bootloader, const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<std::vector<gucc::fs::Partition>, std::string> {
    spdlog::info("Running automatic partitioning");

    const auto esp           = bootloader_esp_layout(bootloader, system_mode);
    const bool is_system_efi = (system_mode == "UEFI"sv);

    auto partitions = gucc::disk::generate_default_partition_schema(device, esp.mountpoint, is_system_efi, esp.size);
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

auto default_zfs_datasets(std::string_view zpool_name) noexcept
    -> std::vector<gucc::fs::ZfsDataset> {
    return {
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT"), zpool_name), .mountpoint = "none"s},
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT/cos"), zpool_name), .mountpoint = "none"s},
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT/cos/root"), zpool_name), .mountpoint = "/"s},
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT/cos/home"), zpool_name), .mountpoint = "/home"s},
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT/cos/varcache"), zpool_name), .mountpoint = "/var/cache"s},
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT/cos/varlog"), zpool_name), .mountpoint = "/var/log"s},
    };
}

auto zfs_auto_pres(std::string_view partition,
    std::string_view zpool_name, std::string_view /*mountpoint*/,
    std::optional<std::string> passphrase) noexcept
    -> std::expected<gucc::fs::ZfsSetupConfig, std::string> {
    gucc::fs::ZfsSetupConfig zfs_setup_config{
        .zpool_name    = std::string(zpool_name),
        .zpool_options = std::string(kDefaultZpoolOptions),
        .passphrase    = std::move(passphrase),
        .datasets      = default_zfs_datasets(zpool_name),
    };

    if (!gucc::fs::zfs_create_with_config(partition, zfs_setup_config)) {
        return std::unexpected("failed to create ZFS automatically");
    }

    return zfs_setup_config;
}

auto apply_zfs_root_layout(std::string_view device, std::string_view zpool_name,
    std::optional<std::string> passphrase, std::string_view system_mode,
    gucc::bootloader::BootloaderType bootloader, std::string_view mountpoint,
    const ExecutionCallbacks& callbacks) noexcept
    -> std::expected<std::string, std::string> {
    const bool is_system_efi = (system_mode == "UEFI"sv);

    // 1. Lay down the partitions (ESP for UEFI + a pool partition), unformatted.
    auto partitions = auto_partition(device, system_mode, bootloader, callbacks);
    if (!partitions) {
        return std::unexpected(partitions.error());
    }
    if (partitions->empty()) {
        return std::unexpected("zfs layout produced no partitions");
    }

    // The pool lives on the last partition (root); the ESP, when present, is first.
    const auto& pool_partition = partitions->back().device;

    // 2. Create the zpool + datasets (optionally encrypted), then import with an
    //    altroot so the datasets mount under `mountpoint`.
    if (auto res = zfs_auto_pres(pool_partition, zpool_name, mountpoint, std::move(passphrase)); !res) {
        return std::unexpected(res.error());
    }
    const auto& import_cmd = fmt::format(FMT_COMPILE("zpool import -R {} {}"), mountpoint, zpool_name);
    if (!gucc::utils::exec_checked(import_cmd)) {
        return std::unexpected(fmt::format("failed to import zpool {}", zpool_name));
    }

    // 3. Format + mount the ESP for UEFI installs.
    if (is_system_efi) {
        const auto esp         = bootloader_esp_layout(bootloader, system_mode);
        const auto& esp_device = partitions->front().device;
        if (auto esp_res = setup_esp_partition(esp_device, esp.mountpoint, mountpoint, true); !esp_res) {
            return std::unexpected(esp_res.error());
        }
    }

    return std::string{zpool_name};
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

auto apply_additional_partitions(const std::vector<AdditionalPartSelection>& additional,
    std::string_view mountpoint, std::vector<gucc::fs::Partition>& partitions) noexcept
    -> std::expected<std::int32_t, std::string> {
    std::int32_t lvm_sep_boot{};

    for (const auto& part_select : additional) {
        if (part_select.format_requested) {
            const auto& mkfs_cmd = fmt::format(FMT_COMPILE("{} {}"), part_select.mkfs_command, part_select.device);
            if (!gucc::utils::exec_checked(mkfs_cmd)) {
                return std::unexpected(fmt::format("failed to format {} with {}", part_select.device, part_select.mkfs_command));
            }
            spdlog::info("Formatted {} with {}", part_select.device, part_select.mkfs_command);
        }

        auto mount_res = mount_partition(part_select.device, mountpoint, part_select.mountpoint, part_select.mount_opts);
        if (!mount_res) {
            return std::unexpected(mount_res.error());
        }

        auto part = gucc::fs::Partition{
            .fstype     = mount_res->fstype.empty() ? part_select.fstype : mount_res->fstype,
            .mountpoint = part_select.mountpoint,
            .device     = part_select.device,
            .mount_opts = part_select.mount_opts,
        };
        part.uuid_str = std::move(mount_res->uuid);
        apply_luks_info(*mount_res, part);
        partitions.emplace_back(std::move(part));

        // Detect separate /boot for grub configuration
        if (part_select.mountpoint == "/boot"sv) {
            lvm_sep_boot             = 1;
            const auto& boot_devices = gucc::disk::list_block_devices();
            if (boot_devices) {
                const auto& boot_dev = gucc::disk::find_device_by_name(*boot_devices, part_select.device);
                if (boot_dev && boot_dev->type == "lvm"sv) {
                    lvm_sep_boot = 2;
                }
            }
        }
    }

    return lvm_sep_boot;
}

auto apply_mount_selections(const MountSelections& selections,
    std::string_view mountpoint) noexcept
    -> std::expected<MountApplicationResult, std::string> {
    spdlog::info("Applying mount selections on {}", mountpoint);
    MountApplicationResult result{};

    // 1. Root partition
    auto root_res = apply_root_partition(selections.root, selections.btrfs_subvolumes, mountpoint);
    if (!root_res) {
        return std::unexpected(root_res.error());
    }
    result.partitions = std::move(root_res->partitions);

    // 2. Swap
    auto swap_res = apply_swap(selections.swap, mountpoint);
    if (!swap_res) {
        return std::unexpected(swap_res.error());
    }
    result.swap_device = std::move(swap_res->swap_device);
    if (swap_res->swap_partition) {
        result.partitions.emplace_back(std::move(*swap_res->swap_partition));
    }

    // 3. Additional partitions
    auto additional_res = apply_additional_partitions(selections.additional, mountpoint, result.partitions);
    if (!additional_res) {
        return std::unexpected(additional_res.error());
    }
    result.lvm_sep_boot = *additional_res;

    // 4. ESP
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
        return std::unexpected(gucc::to_string(boot_part_struct.error()));
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

auto mount_partition(std::string_view partition, std::string_view mountpoint,
    std::string_view mount_dev, std::string_view mount_opts) noexcept
    -> std::expected<MountPartitionResult, std::string> {
    const auto& mount_dir = fmt::format(FMT_COMPILE("{}{}"), mountpoint, mount_dev);

    std::error_code ec{};
    std::filesystem::create_directories(mount_dir, ec);
    if (ec) {
        return std::unexpected(fmt::format("failed to create directory {}: {}", mount_dir, ec.message()));
    }

    // TODO(vnepogodin): use libmount instead.
    // see https://github.com/util-linux/util-linux/blob/master/sys-utils/mount.c#L734
    if (auto res = gucc::mount::mount_partition(partition, mount_dir, mount_opts); !res) {
        return std::unexpected(fmt::format("failed to mount partition {} at {} with opts '{}': {}", partition, mount_dir, mount_opts, gucc::to_string(res.error())));
    }

    // Actual mounted device may differ from partition (e.g. for mapper devices)
    const auto& mounted_device = gucc::fs::utils::get_mountpoint_source(mount_dir);
    const auto& query_device   = mounted_device.empty() ? partition : mounted_device;

    MountPartitionResult result{};
    // returns negative for non-LUKS. ignore
    gucc::mount::query_partition(query_device, result.is_luks, result.is_lvm,
        result.luks_name, result.luks_dev, result.luks_uuid);

    result.fstype = gucc::fs::utils::get_mountpoint_fs(mount_dir);
    result.uuid   = gucc::fs::utils::get_device_uuid(partition);

    spdlog::debug("mounted '{}' at '{}': is_luks={};is_lvm={};luks_name='{}';fstype='{}';uuid='{}'",
        partition, mount_dir, result.is_luks, result.is_lvm, result.luks_name, result.fstype, result.uuid);
    return result;
}

auto apply_swap(const SwapSelection& swap,
    std::string_view mountpoint) noexcept
    -> std::expected<SwapApplicationResult, std::string> {
    SwapApplicationResult result{};

    switch (swap.type) {
    case SwapSelection::Type::Swapfile: {
        const auto& swapfile_path = fmt::format(FMT_COMPILE("{}/swapfile"), mountpoint);
        if (!gucc::swap::make_swapfile(mountpoint, swap.swapfile_size)) {
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
            .device     = swap.device,
            .mount_opts = std::move(swap_opts),
        };
        if (!gucc::swap::make_swap_partition(swap_part)) {
            return std::unexpected("failed to create swap partition");
        }
        result.swap_device    = swap.device;
        swap_part.uuid_str    = gucc::fs::utils::get_device_uuid(swap.device);
        result.swap_partition = std::move(swap_part);
        break;
    }
    case SwapSelection::Type::None:
        break;
    }
    return result;
}

auto apply_root_partition(const RootPartitionSelection& selection,
    const std::vector<gucc::fs::BtrfsSubvolume>& btrfs_subvols,
    std::string_view mountpoint) noexcept
    -> std::expected<RootPartitionResult, std::string> {
    // 1. Format if requested
    if (selection.format_requested) {
        // Fall back to deriving the mkfs command from the fstype
        std::string mkfs_program{selection.mkfs_command};
        if (mkfs_program.empty()) {
            mkfs_program = std::string{gucc::fs::get_mkfs_command(gucc::fs::string_to_filesystem_type(selection.fstype))};
        }
        if (mkfs_program.empty()) {
            return std::unexpected(fmt::format("no mkfs command for root partition {} (fstype '{}')", selection.device, selection.fstype));
        }
        const auto& mkfs_cmd = fmt::format(FMT_COMPILE("{} {}"), mkfs_program, selection.device);
        if (!gucc::utils::exec_checked(mkfs_cmd)) {
            return std::unexpected(fmt::format("failed to format root partition {} with {}", selection.device, mkfs_program));
        }
        spdlog::info("Formatted {} with {}", selection.device, mkfs_program);
    }

    // 2. Mount root partition
    auto mount_res = mount_partition(selection.device, mountpoint, "/"sv, selection.mount_opts);
    if (!mount_res) {
        return std::unexpected(fmt::format("failed to mount root partition {}: {}", selection.device, mount_res.error()));
    }

    // 3. Build root partition struct
    std::string part_fs;
    if (!mount_res->fstype.empty()) {
        part_fs = mount_res->fstype;
    } else if (selection.fstype == "skip"sv) {
        part_fs = "unknown"s;
    } else {
        part_fs = selection.fstype;
    }

    auto root_part = gucc::fs::Partition{
        .fstype     = std::move(part_fs),
        .mountpoint = "/"s,
        .device     = selection.device,
        .mount_opts = selection.mount_opts,
    };
    root_part.uuid_str = mount_res->uuid;
    apply_luks_info(*mount_res, root_part);

    RootPartitionResult result{};
    result.mount_info = std::move(*mount_res);
    result.partitions.emplace_back(std::move(root_part));

    // 4. Handle BTRFS subvolumes
    if (!btrfs_subvols.empty()) {
        auto btrfs_res = apply_btrfs_subvolumes(btrfs_subvols, selection, mountpoint, result.partitions);
        if (!btrfs_res) {
            return std::unexpected(btrfs_res.error());
        }
    }

    return result;
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

auto list_mounted_devices(std::string_view mountpoint) noexcept -> std::string {
    const auto& devices = gucc::disk::list_block_devices();
    if (!devices) {
        spdlog::error("failed to find block devices");
        return {};
    }
    const auto& mounted_names = gucc::disk::list_mounted_devices(*devices, mountpoint);
    return gucc::utils::join(mounted_names);
}

auto list_containing_crypt() noexcept -> std::string {
    return gucc::utils::exec("blkid | awk '/TYPE=\"crypto_LUKS\"/{print $1}' | sed 's/.$//'");
}

auto list_non_crypt() noexcept -> std::string {
    return gucc::utils::exec("blkid | awk '!/TYPE=\"crypto_LUKS\"/{print $1}' | sed 's/.$//'");
}

}  // namespace cachyos::installer
