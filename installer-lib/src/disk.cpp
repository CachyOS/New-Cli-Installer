#include "cachyos/disk.hpp"
#include "cachyos/packages.hpp"
#include "cachyos/result.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/btrfs.hpp"
#include "gucc/crypto_detection.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/fstab.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/mount_partitions.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/partitioning.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/swap.hpp"
#include "gucc/system_query.hpp"
#include "gucc/umount_partitions.hpp"
#include "gucc/zfs.hpp"

#include <algorithm>    // for transform, sort
#include <expected>     // for unexpected
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

using cachyos::installer::lift;
using cachyos::installer::MountPartitionResult;

/// Default ZFS pool creation options shared by all ZFS setup paths.
constexpr auto kDefaultZpoolOptions{"-f -o ashift=12 -o autotrim=on -O mountpoint=none -O acltype=posixacl -O atime=off -O relatime=off -O xattr=sa -O normalization=formD -O dnodesize=auto"sv};

/// Resolve a partition to the best stable device path for zpool creation.
auto resolve_zfs_vdev(std::string_view device) noexcept -> std::string {
    if (const auto& blk = cachyos::installer::block_devices(); blk) {
        const auto& dev = gucc::disk::find_device_by_name(*blk, device);
        if (dev && dev->partuuid && !dev->partuuid->empty()) {
            return fmt::format(FMT_COMPILE("/dev/disk/by-partuuid/{}"), gucc::utils::to_lower(*dev->partuuid));
        }
    }
    return std::string(device);
}

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

/// Runs the selection's mkfs command when formatting was requested.
template <class Selection>
auto format_if_requested(const Selection& selection) noexcept -> std::expected<void, std::string> {
    if (!selection.format_requested || selection.mkfs_command.empty()) {
        return {};
    }
    const auto format_cmd = fmt::format(FMT_COMPILE("{} {}"), selection.mkfs_command, selection.device);
    if (auto res = lift(gucc::utils::exec_or_error(format_cmd, "format partition"sv)); !res) {
        return res;
    }
    spdlog::info("Formatted {} with {}", selection.device, selection.mkfs_command);
    // let blkid see the fresh filesystem before we mount it
    gucc::utils::settle_devices();
    return {};
}

// "skip" keeps whatever filesystem is already on the partition
constexpr auto fstype_or_unknown(std::string_view fstype) noexcept -> std::string {
    return fstype == "skip"sv ? "unknown"s : std::string{fstype};
}

/// Partition entry for @p selection mounted at @p mountpoint.
template <class Selection>
auto mounted_partition(const Selection& selection, std::string_view mountpoint, const MountPartitionResult& mount_res) noexcept
    -> gucc::fs::Partition {
    auto part = gucc::fs::Partition{
        .fstype     = !mount_res.fstype.empty() ? mount_res.fstype : fstype_or_unknown(selection.fstype),
        .mountpoint = std::string{mountpoint},
        .device     = selection.device,
        .mount_opts = selection.mount_opts,
    };
    part.uuid_str = mount_res.uuid;
    if (mount_res.is_luks && !mount_res.luks_name.empty()) {
        part.luks_mapper_name = mount_res.luks_name;
    }
    if (mount_res.is_luks && !mount_res.luks_uuid.empty()) {
        part.luks_uuid = mount_res.luks_uuid;
    }
    return part;
}

auto blkid_devices(bool is_luks) noexcept -> std::string {
    return gucc::utils::query_output("blkid -o export"sv)
        .transform([is_luks](std::string_view output) { return gucc::utils::join(cachyos::installer::detail::blkid_devices_by_luks(output, is_luks)); })
        .value_or(std::string{});
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

auto block_devices() noexcept -> std::expected<std::vector<gucc::disk::BlockDevice>, std::string> {
    auto devices = gucc::disk::list_block_devices();
    if (!devices) {
        return std::unexpected("failed to find block devices");
    }
    return *std::move(devices);
}

auto find_mount_at(std::string_view path) noexcept -> std::optional<gucc::mtab::MTabEntry> {
    // the kernel table never carries a trailing slash, findmnt used to normalise it for us
    path                = gucc::utils::strip_trailing_slashes(path);
    const auto& entries = gucc::mtab::parse_mtab(path, "/proc/self/mounts"sv);
    return entries ? gucc::mtab::find_mount(*entries, path) : std::nullopt;
}

auto is_volume_removable(std::string_view mountpoint) noexcept -> bool {
    const auto& devices = block_devices();
    if (!devices) {
        spdlog::error("Failed to list block devices for removable check");
        return false;
    }
    return is_volume_removable(*devices, mountpoint);
}

auto is_volume_removable(const std::vector<gucc::disk::BlockDevice>& devices, std::string_view mountpoint) noexcept -> bool {
    const auto& mounted_dev = gucc::disk::find_device_by_mountpoint(devices, mountpoint);
    if (!mounted_dev) {
        spdlog::error("No block device found at mountpoint {}", mountpoint);
        return false;
    }

    // Walk the pkname chain (handles mapper → crypt → part → disk) to find the parent disk
    const auto& disk_dev = gucc::disk::find_ancestor_of_type(devices, mounted_dev->name, "disk"sv);
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
    gucc::bootloader::BootloaderType bootloader) noexcept
    -> std::expected<std::vector<gucc::fs::Partition>, std::string> {
    spdlog::info("Running automatic partitioning");

    const auto& boot_mountpoint = bootloader_default_mount(bootloader, system_mode);
    const bool is_system_efi    = (system_mode == "UEFI"sv);

    auto partitions = gucc::disk::generate_default_partition_schema(device, boot_mountpoint, is_system_efi);
    if (partitions.empty()) {
        return std::unexpected("failed to generate default partition schema: it cannot be empty");
    }

    if (auto res = lift(gucc::disk::make_clean_partschema(device, partitions, is_system_efi), "failed to perform automatic partitioning"); !res) {
        return std::unexpected(std::move(res.error()));
    }

    return partitions;
}

auto secure_wipe(std::string_view device) noexcept
    -> std::expected<void, std::string> {
    // Ensure wipe tool is installed
    if (auto res = install_needed("wipe"); !res) {
        return res;
    }
    return lift(gucc::utils::exec_or_error(fmt::format(FMT_COMPILE("wipe -Ifre {}"), device), "wipe device"sv));
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

auto default_zfs_setup(std::string_view zpool_name,
    std::optional<std::string> passphrase) noexcept
    -> gucc::fs::ZfsSetupConfig {
    return gucc::fs::ZfsSetupConfig{
        .zpool_name    = std::string(zpool_name),
        .zpool_options = std::string(kDefaultZpoolOptions),
        .passphrase    = std::move(passphrase),
        .datasets      = default_zfs_datasets(zpool_name),
    };
}

auto zfs_auto_pres(std::string_view partition,
    std::string_view zpool_name, std::string_view /*mountpoint*/) noexcept
    -> std::expected<gucc::fs::ZfsSetupConfig, std::string> {
    // passphrase should be known at this time, e.g. passing as arg to zfs_auto_pres func
    gucc::fs::ZfsSetupConfig zfs_setup_config{
        .zpool_name    = std::string(zpool_name),
        .zpool_options = std::string(kDefaultZpoolOptions),
        .passphrase    = std::nullopt,
        .datasets      = default_zfs_datasets(zpool_name),
    };

    if (auto res = lift(gucc::fs::zfs_create_with_config(partition, zfs_setup_config)); !res) {
        return std::unexpected(std::move(res.error()));
    }

    return zfs_setup_config;
}

auto zfs_create_zpool(std::string_view partition,
    std::string_view pool_name, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    if (auto res = lift(gucc::fs::zfs_create_zpool(resolve_zfs_vdev(partition), pool_name, kDefaultZpoolOptions)); !res) {
        return res;
    }
    // Since zfs manages mountpoints, export and re-import with root at mountpoint
    if (auto res = lift(gucc::fs::zpool_export(pool_name)); !res) {
        return res;
    }
    return lift(gucc::fs::zpool_import(pool_name, mountpoint));
}

auto apply_zfs_root(const gucc::fs::ZfsSetupConfig& zfs_setup,
    std::string_view root_device, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    const auto& vdev = resolve_zfs_vdev(root_device);
    if (auto res = lift(gucc::fs::zfs_create_with_config(vdev, zfs_setup)); !res) {
        return res;
    }

    // catch up exported pool before re-import
    gucc::utils::settle_devices();

    // re-import without encrypt key to load it separately
    if (auto res = lift(gucc::fs::zpool_import(zfs_setup.zpool_name, mountpoint, false)); !res) {
        return res;
    }

    if (zfs_setup.passphrase) {
        if (auto res = lift(gucc::fs::zfs_load_key(zfs_setup.zpool_name, *zfs_setup.passphrase)); !res) {
            return res;
        }
    }

    // manually mount datasets in order
    auto mountable = zfs_setup.datasets
        | std::ranges::views::filter([](const gucc::fs::ZfsDataset& ds) { return ds.mountpoint != "none"sv && ds.mountpoint != "legacy"sv; })
        | std::ranges::to<std::vector<gucc::fs::ZfsDataset>>();

    std::ranges::sort(mountable, {}, &gucc::fs::ZfsDataset::mountpoint);
    for (const auto& dataset : mountable) {
        if (auto res = lift(gucc::fs::zfs_mount(dataset.zpath)); !res) {
            return res;
        }
    }

    return {};
}

auto apply_additional_partitions(const std::vector<AdditionalPartSelection>& additional,
    std::string_view mountpoint, std::vector<gucc::fs::Partition>& partitions) noexcept
    -> std::expected<std::int32_t, std::string> {
    std::int32_t lvm_sep_boot{};

    for (const auto& part_select : additional) {
        if (auto res = format_if_requested(part_select); !res) {
            return std::unexpected(std::move(res.error()));
        }

        const auto mount_res = mount_partition(part_select.device, mountpoint, part_select.mountpoint, part_select.mount_opts);
        if (!mount_res) {
            return std::unexpected(mount_res.error());
        }
        partitions.emplace_back(mounted_partition(part_select, part_select.mountpoint, *mount_res));

        // Detect separate /boot for grub configuration
        if (part_select.mountpoint == "/boot"sv) {
            lvm_sep_boot = 1;
            if (const auto& boot_devices = block_devices(); boot_devices) {
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
    if (auto res = lift(gucc::fs::btrfs_create_subvols(subvols, selection.device, mountpoint, selection.mount_opts)); !res) {
        return res;
    }
    return lift(gucc::fs::btrfs_append_subvolumes(partitions, subvols));
}

auto setup_esp_partition(std::string_view device, std::string_view mountpoint,
    std::string_view root_mountpoint, bool format_requested) noexcept
    -> std::expected<gucc::fs::Partition, std::string> {
    const bool is_boot_ssd = gucc::disk::is_device_ssd(device);
    return lift(gucc::mount::setup_esp_partition(device, mountpoint, root_mountpoint, format_requested, is_boot_ssd));
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

    return lift(gucc::umount::umount_partitions(mountpoint, zfs_pools));
}

auto generate_fstab(std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    spdlog::info("Generating fstab on {}", mountpoint);
    return lift(gucc::fs::run_genfstab_on_mount(mountpoint));
}

auto mount_partition(std::string_view partition, std::string_view mountpoint,
    std::string_view mount_dev, std::string_view mount_opts, std::string_view fstype) noexcept
    -> std::expected<MountPartitionResult, std::string> {
    const auto& mount_dir = gucc::utils::target_path(mountpoint, mount_dev);
    if (auto res = lift(gucc::file_utils::create_directories(mount_dir)); !res) {
        return std::unexpected(std::move(res.error()));
    }

    // resolve the gucc fs-name to the kernel mount type
    const auto& fs_kernel = fstype.empty()
        ? std::string_view{}
        : gucc::fs::get_fstab_fs_name(gucc::fs::string_to_filesystem_type(fstype));

    // TODO(vnepogodin): use libmount instead.
    // see https://github.com/util-linux/util-linux/blob/master/sys-utils/mount.c#L734
    if (auto res = lift(gucc::mount::mount_partition(partition, mount_dir, mount_opts, fs_kernel),
            fmt::format(FMT_COMPILE("failed to mount partition {} at {} with opts '{}'"), partition, mount_dir, mount_opts));
        !res) {
        return std::unexpected(std::move(res.error()));
    }

    // one kernel mount table read + one lsblk snapshot answer every query below
    const auto& mount   = find_mount_at(mount_dir);
    const auto& devices = block_devices();

    MountPartitionResult result{};
    if (mount) {
        result.fstype = mount->fstype;
    }

    // Actual mounted device may differ from partition (e.g. for mapper devices)
    const std::string_view query_device = mount ? std::string_view{mount->device} : partition;
    const auto& crypto                  = devices ? gucc::disk::detect_crypto_for_device(*devices, query_device) : std::nullopt;
    if (crypto && crypto->is_luks) {
        result.is_luks   = 1;
        result.is_lvm    = crypto->is_lvm ? 1 : 0;
        result.luks_name = crypto->luks_mapper_name;
        result.luks_dev  = crypto->luks_dev;
        result.luks_uuid = crypto->luks_uuid;
    }

    // lsblk filters out whole disks and loop devices
    const auto& part_dev = devices ? gucc::disk::find_device_by_name(*devices, partition) : std::nullopt;
    result.uuid          = part_dev ? part_dev->uuid : gucc::fs::utils::get_device_uuid(partition);

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
        const auto& swapfile_path = gucc::utils::target_path(mountpoint, "/swapfile"sv);
        if (auto res = lift(gucc::swap::make_swapfile(mountpoint, swap.swapfile_size)); !res) {
            return std::unexpected(std::move(res.error()));
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
        if (auto res = lift(gucc::swap::make_swap_partition(swap_part)); !res) {
            return std::unexpected(std::move(res.error()));
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
    if (selection.device.empty()) {
        return RootPartitionResult{};
    }

    if (auto res = format_if_requested(selection); !res) {
        return std::unexpected(fmt::format(FMT_COMPILE("root partition: {}"), res.error()));
    }

    auto mount_res = mount_partition(selection.device, mountpoint, "/"sv, selection.mount_opts, selection.fstype);
    if (!mount_res) {
        return std::unexpected(fmt::format(FMT_COMPILE("failed to mount root partition {}: {}"), selection.device, mount_res.error()));
    }

    RootPartitionResult result{};
    result.partitions.emplace_back(mounted_partition(selection, "/"sv, *mount_res));
    result.mount_info = *std::move(mount_res);

    // Handle BTRFS subvolumes
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
    const auto& devices = block_devices();
    if (!devices) {
        spdlog::error("{}", devices.error());
        return {};
    }
    const auto& mounted_names = gucc::disk::list_mounted_devices(*devices, mountpoint);
    return gucc::utils::join(mounted_names);
}

auto list_containing_crypt() noexcept -> std::string {
    return blkid_devices(true);
}

auto list_non_crypt() noexcept -> std::string {
    return blkid_devices(false);
}

}  // namespace cachyos::installer

namespace cachyos::installer::detail {

auto blkid_devices_by_luks(std::string_view blkid_export, bool is_luks) noexcept -> std::vector<std::string> {
    // TODO(vnepogodin): shouldn't be also JSON'ed???

    // one block of `KEY=value` lines per device, blocks separated by an empty line
    const auto matching_device = [is_luks](std::string_view block) -> std::optional<std::string> {
        auto lines         = gucc::utils::make_split_view(block);
        const auto devname = gucc::utils::value_of(lines, "DEVNAME"sv);
        if (!devname || (gucc::utils::value_of(lines, "TYPE"sv) == "crypto_LUKS"sv) != is_luks) {
            return std::nullopt;
        }
        return std::string{*devname};
    };
    return gucc::utils::make_split_view(blkid_export, "\n\n"sv)
        | std::ranges::views::transform(matching_device)
        | std::ranges::views::join
        | std::ranges::to<std::vector<std::string>>();
}

}  // namespace cachyos::installer::detail
