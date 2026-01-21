#include "disk.hpp"
#include "config.hpp"
#include "utils.hpp"
#include "widgets.hpp"

// import gucc
#include "gucc/btrfs.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/mount_partitions.hpp"
#include "gucc/partition.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/zfs.hpp"

#include <algorithm>   // for find_if
#include <filesystem>  // for create_directories
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

namespace fs = std::filesystem;

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

void btrfs_create_subvols(std::vector<gucc::fs::Partition>& partitions, const std::string_view& mode, bool ignore_note) noexcept {
    /* clang-format off */
    if (mode.empty()) { return; }
    /* clang-format on */

    const std::vector<gucc::fs::BtrfsSubvolume> default_subvolumes{
        gucc::fs::BtrfsSubvolume{.subvolume = "/@"s, .mountpoint = "/"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@home"s, .mountpoint = "/home"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@root"s, .mountpoint = "/root"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@srv"s, .mountpoint = "/srv"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@cache"s, .mountpoint = "/var/cache"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@tmp"s, .mountpoint = "/var/tmp"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@log"s, .mountpoint = "/var/log"s},
        // gucc::fs::BtrfsSubvolume{.subvolume = "/@snapshots"sv, .mountpoint = "/.snapshots"sv},
    };

    auto root_part = find_root_btrfs_part(partitions);
    if (root_part == std::ranges::end(partitions)) {
        spdlog::error("btrfs_create_subvols: unable to find btrfs root part");
        return;
    }

#ifdef NDEVENV
    const auto root_mountpoint = "/mnt"sv;

    // save mount options and name of the root partition
    gucc::utils::exec(R"(mount | grep 'on /mnt ' | grep -Po '(?<=\().*(?=\))' > /tmp/.root_mount_options)"sv);
    // gucc::utils::exec("lsblk -lno MOUNTPOINT,NAME | awk '/^\\/mnt / {print $2}' > /tmp/.root_partition"sv);

    if (mode == "manual"sv) {
        // Create subvolumes manually
        std::string subvols{"/@ /@home /@cache"};
        static constexpr auto subvols_body = "\nInput names of the subvolumes separated by spaces.\nThe first one will be used for mounting /.\n"sv;
        if (!tui::detail::inputbox_widget(subvols, subvols_body, size(ftxui::HEIGHT, ftxui::GREATER_THAN, 4))) {
            return;
        }
        auto subvol_list = gucc::utils::make_multiline(subvols, false, ' ');

        // Make the first subvolume with mountpoint /
        std::vector<gucc::fs::BtrfsSubvolume> subvolumes{
            gucc::fs::BtrfsSubvolume{.subvolume = std::move(subvol_list[0]), .mountpoint = "/"s},
        };

        // Remove the first subvolume from the subvolume list
        subvol_list.erase(subvol_list.begin());

        // Get mountpoints of subvolumes
        for (auto&& subvol : subvol_list) {
            // Ask for mountpoint
            const auto& content = fmt::format(FMT_COMPILE("\nInput mountpoint of\nthe subvolume {}\nas it would appear\nin installed system\n(without prepending {}).\n"), subvol, root_mountpoint);
            std::string mountpoint{"/home"};
            if (!tui::detail::inputbox_widget(mountpoint, content, size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
                return;
            }

            subvolumes.push_back(gucc::fs::BtrfsSubvolume{.subvolume = std::move(subvol), .mountpoint = std::move(mountpoint)});
        }

        // Create subvolumes
        if (!gucc::fs::btrfs_create_subvols(subvolumes, root_part->device, root_mountpoint, root_part->mount_opts)) {
            spdlog::error("Failed to create subvolumes");
        }
        if (!gucc::fs::btrfs_append_subvolumes(partitions, subvolumes)) {
            spdlog::error("Failed to append btrfs subvolumes into partition scheme");
        }

        // need to find it again, due to modifying the parts
        root_part = find_root_btrfs_part(partitions);
        utils::dump_partition_to_log(*root_part);
        return;
    }
    if (!ignore_note) {
        static constexpr auto content = "\nThis creates subvolumes:\n/@ for /,\n/@home for /home,\n/@cache for /var/cache.\n"sv;
        const auto& do_create         = tui::detail::yesno_widget(content, size(ftxui::HEIGHT, ftxui::LESS_THAN, 15) | size(ftxui::WIDTH, ftxui::LESS_THAN, 75));
        /* clang-format off */
        if (!do_create) { return; }
        /* clang-format on */
    }

    // Create subvolumes automatically
    if (!gucc::fs::btrfs_create_subvols(default_subvolumes, root_part->device, root_mountpoint, root_part->mount_opts)) {
        spdlog::error("Failed to create subvolumes automatically");
    }
#else
    spdlog::info("Do we ignore note? {}", ignore_note);
#endif

    if (!gucc::fs::btrfs_append_subvolumes(partitions, default_subvolumes)) {
        spdlog::error("Failed to append btrfs subvolumes into partition scheme");
    }

    // need to find it again, due to modifying the parts
    root_part = find_root_btrfs_part(partitions);
    utils::dump_partition_to_log(*root_part);
}

void mount_existing_subvols(std::vector<gucc::fs::Partition>& partitions) noexcept {
    auto root_part = find_root_btrfs_part(partitions);
    if (root_part == std::ranges::end(partitions)) {
        spdlog::error("mount_existing_subvols: unable to find btrfs root part");
        return;
    }

    // Set mount options
    const auto& part_dev_name    = gucc::utils::exec(fmt::format(FMT_COMPILE("echo {} | rev | cut -d/ -f1 | rev"), root_part->device));
    const auto& device_name      = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/{}/,/disk/p' | {}"), part_dev_name, "awk '/disk/ {print $1}'"sv));
    const auto& rotational_queue = (gucc::utils::exec(fmt::format(FMT_COMPILE("cat /sys/block/{}/queue/rotational"), device_name)) == "1"sv);

    std::string fs_opts{};
    if (rotational_queue) {
        fs_opts = "autodefrag,compress=zlib,noatime,nossd,commit=120"sv;
    } else {
        fs_opts = "compress=lzo,noatime,space_cache,ssd,commit=120"sv;
    }
#ifdef NDEVENV
    const auto root_mountpoint = "/mnt"sv;

    gucc::utils::exec(fmt::format(FMT_COMPILE("btrfs subvolume list {} 2>/dev/null | cut -d' ' -f9 > /tmp/.subvols"), root_mountpoint), true);
    if (!gucc::utils::exec_checked(fmt::format(FMT_COMPILE("umount -v {} &>>/tmp/cachyos-install.log"), root_mountpoint))) {
        spdlog::error("Failed to unmount {}", root_mountpoint);
    }

    const auto& subvol_list = gucc::utils::make_multiline(gucc::utils::exec("cat /tmp/.subvols"sv));

    // Get mountpoints of subvolumes
    std::vector<gucc::fs::BtrfsSubvolume> subvolumes{};
    for (auto&& subvol : subvol_list) {
        // Ask for mountpoint
        const auto& content = fmt::format(FMT_COMPILE("\nInput mountpoint of\nthe subvolume {}\nas it would appear\nin installed system\n(without prepending {}).\n"), subvol, root_mountpoint);
        std::string mountpoint{"/"};
        if (!tui::detail::inputbox_widget(mountpoint, content, size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
            return;
        }

        subvolumes.push_back(gucc::fs::BtrfsSubvolume{.subvolume = std::move(subvol), .mountpoint = std::move(mountpoint)});
    }

    // TODO(vnepogodin): add confirmation for selected mountpoint for particular subvolume

    // Mount subvolumes
    if (!gucc::fs::btrfs_mount_subvols(subvolumes, root_part->device, root_mountpoint, fs_opts)) {
        spdlog::error("Failed to mount btrfs subvolumes");
    }

    if (!gucc::fs::btrfs_append_subvolumes(partitions, subvolumes)) {
        spdlog::error("Failed to append btrfs subvolumes into partition scheme");
    }

    // need to find it again, due to modifying the parts
    root_part = find_root_btrfs_part(partitions);
    utils::dump_partition_to_log(*root_part);
#endif
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
    static constexpr auto zpool_options{"-f -o ashift=12 -o autotrim=on -O mountpoint=none -O acltype=posixacl -O atime=off -O relatime=off -O xattr=sa -O normalization=formD -O dnodesize=auto"sv};

    // next create the datasets including their parents
    const std::vector<gucc::fs::ZfsDataset> default_zfs_datasets{
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT"), zfs_zpool_name), .mountpoint = "none"s},
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT/cos"), zfs_zpool_name), .mountpoint = "none"s},
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT/cos/root"), zfs_zpool_name), .mountpoint = "/"s},
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT/cos/home"), zfs_zpool_name), .mountpoint = "/home"s},
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT/cos/varcache"), zfs_zpool_name), .mountpoint = "/var/cache"s},
        gucc::fs::ZfsDataset{.zpath = fmt::format(FMT_COMPILE("{}/ROOT/cos/varlog"), zfs_zpool_name), .mountpoint = "/var/log"s},
    };
    // passphrase should be known at this time, e.g. passing as arg to zfs_auto_pres func
    const gucc::fs::ZfsSetupConfig zfs_setup_config{.zpool_name = std::string(zfs_zpool_name), .zpool_options = std::string(zpool_options), .passphrase = std::nullopt, .datasets = default_zfs_datasets};

#ifdef NDEVENV
    if (!gucc::fs::zfs_create_with_config(partition, zfs_setup_config)) {
        spdlog::error("Failed to create ZFS automatically");
        return false;
    }
#else
    spdlog::info("Created ZFS automatically with: {}", zfs_setup_config);
#endif

    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    config_data["ZFS"]    = 1;

    // save setup config
    config_data["ZFS_SETUP_CONFIG"] = zfs_setup_config;

    // insert zpool name into config
    auto& zfs_zpool_names = std::get<std::vector<std::string>>(config_data["ZFS_ZPOOL_NAMES"]);
    zfs_zpool_names.emplace_back(zfs_zpool_name);

    return true;
}

// creates a new zpool on an existing partition
bool zfs_create_zpool(const std::string_view& partition, const std::string_view& pool_name) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    static constexpr auto zpool_options{"-f -o ashift=12 -o autotrim=on -O mountpoint=none -O acltype=posixacl -O atime=off -O relatime=off -O xattr=sa -O normalization=formD -O dnodesize=auto"sv};

#ifdef NDEVENV
    std::string device_path{partition};

    // Find the UUID of the partition
    const auto& partuuid = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -lno PATH,PARTUUID | grep \"^{}\" | {}"), partition, "awk '{print $2}'"sv), false);

    // See if the partition has a partuuid, if not use the device name
    if (!partuuid.empty()) {
        device_path = partuuid;
    }

    if (!gucc::fs::zfs_create_zpool(device_path, pool_name, zpool_options)) {
        spdlog::error("Failed to create zpool!");
        return false;
    }
#else
    spdlog::info("zfs zpool options := '{}', on '{}'", zpool_options, partition);
#endif

    config_data["ZFS"] = 1;

#ifdef NDEVENV
    // Since zfs manages mountpoints, we export it and then import with a root of MOUNTPOINT
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    gucc::utils::exec(fmt::format(FMT_COMPILE("zpool export {} 2>>/tmp/cachyos-install.log"), pool_name), true);
    gucc::utils::exec(fmt::format(FMT_COMPILE("zpool import -R {} {} 2>>/tmp/cachyos-install.log"), mountpoint, pool_name), true);
#endif

    // insert zpool name into config
    auto& zfs_zpool_names = std::get<std::vector<std::string>>(config_data["ZFS_ZPOOL_NAMES"]);
    zfs_zpool_names.emplace_back(pool_name.data());

    return true;
}

// Other filesystems
void select_filesystem(const std::string_view& file_sys) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    config_data["FILESYSTEM_NAME"] = std::string{file_sys.data()};

    const auto& available_mount_opts = utils::get_available_mount_opts(file_sys);
    if (file_sys == "btrfs"sv) {
        config_data["FILESYSTEM"] = "mkfs.btrfs -f";
        config_data["fs_opts"]    = available_mount_opts;
#ifdef NDEVENV
        gucc::utils::exec("modprobe btrfs"sv);
#endif
    } else if (file_sys == "ext4"sv) {
        config_data["FILESYSTEM"] = "mkfs.ext4 -q";
        config_data["fs_opts"]    = available_mount_opts;
    } else if (file_sys == "f2fs"sv) {
        config_data["FILESYSTEM"] = "mkfs.f2fs -q";
        config_data["fs_opts"]    = available_mount_opts;
#ifdef NDEVENV
        gucc::utils::exec("modprobe f2fs"sv);
#endif
    } else if (file_sys == "xfs"sv) {
        config_data["FILESYSTEM"] = "mkfs.xfs -f";
        config_data["fs_opts"]    = available_mount_opts;
    } else if (file_sys != "zfs"sv) {
        spdlog::error("Invalid filesystem ('{}')!", file_sys);
    }
}

auto get_available_mount_opts(std::string_view fstype) noexcept -> std::vector<std::string> {
    if (fstype == "btrfs"sv) {
        return std::vector<std::string>{"autodefrag", "compress=zlib", "compress=lzo", "compress=zstd", "compress=no", "compress-force=zlib", "compress-force=lzo", "compress-force=zstd", "discard", "noacl", "noatime", "nodatasum", "nospace_cache", "recovery", "skip_balance", "space_cache", "nossd", "ssd", "ssd_spread", "commit=120"};
    } else if (fstype == "ext4"sv) {
        return std::vector<std::string>{"data=journal", "data=writeback", "dealloc", "discard", "noacl", "noatime", "nobarrier", "nodelalloc"};
    } else if (fstype == "f2fs"sv) {
        return std::vector<std::string>{"data_flush", "disable_roll_forward", "disable_ext_identify", "discard", "fastboot", "flush_merge", "inline_xattr", "inline_data", "inline_dentry", "no_heap", "noacl", "nobarrier", "noextent_cache", "noinline_data", "norecovery"};
    } else if (fstype == "xfs"sv) {
        return std::vector<std::string>{"discard", "filestreams", "ikeep", "largeio", "noalign", "nobarrier", "norecovery", "noquota", "wsync"};
    }

    return {};
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
    // NOTE: for /mnt on /dev/mapper/cryptroot `root_name` will be cryptroot
    const auto& root_name = gucc::utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");

    // NOTE: for /mnt on /dev/mapper/cryptroot on /dev/sda2 with `root_name`=cryptroot, `root_device` will be sda
    const auto& root_device = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/{}/,/disk/p' | {}"), root_name, "awk '/disk/ {print $1}'"));
    spdlog::info("root_name: {}. root_device: {}", root_name, root_device);
    const auto& removable = gucc::utils::exec(fmt::format(FMT_COMPILE("cat /sys/block/{}/removable"), root_device));
    return utils::to_int(removable) == 1;
}

}  // namespace utils
