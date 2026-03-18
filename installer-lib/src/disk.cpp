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
#include "gucc/system_query.hpp"
#include "gucc/umount_partitions.hpp"
#include "gucc/zfs.hpp"

#include <algorithm>    // for transform
#include <charconv>     // for from_chars
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

// TODO(vnepogodin): refactor to util header/module
template <typename T = std::int32_t>
    requires std::numeric_limits<T>::is_integer
inline T to_int(const std::string_view& str) {
    T result = 0;
    std::from_chars(str.data(), str.data() + str.size(), result);
    return result;
}

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

auto is_volume_removable(std::string_view device) noexcept -> bool {
    // NOTE: for /mnt on /dev/mapper/cryptroot `root_name` will be cryptroot
    const auto& root_name = gucc::utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");

    // NOTE: for /mnt on /dev/mapper/cryptroot on /dev/sda2 with `root_name`=cryptroot, `root_device` will be sda
    const auto& root_device = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/{}/,/disk/p' | {}"), root_name, "awk '/disk/ {print $1}'"));
    spdlog::info("root_name: {}. root_device: {}", root_name, root_device);
    const auto& removable = gucc::utils::exec(fmt::format(FMT_COMPILE("cat /sys/block/{}/removable"), root_device));
    return to_int(removable) == 1;
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

auto secure_wipe(std::string_view device,
    const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<void, std::string> {
    // Ensure wipe tool is installed
    auto needed_result = install_needed("wipe", {});
    if (!needed_result) {
        return std::unexpected(needed_result.error());
    }

    const auto& cmd = fmt::format(FMT_COMPILE("wipe -Ifre {}"), device);
    if (!gucc::utils::exec_checked(cmd)) {
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

auto apply_mount_selections(const MountSelections& /*selections*/,
    std::string_view /*mountpoint*/) noexcept
    -> std::expected<std::vector<gucc::fs::Partition>, std::string> {
    return std::unexpected("not yet implemented");
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

}  // namespace cachyos::installer
