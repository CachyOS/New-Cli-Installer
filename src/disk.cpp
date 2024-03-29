#include "disk.hpp"
#include "config.hpp"
#include "utils.hpp"
#include "widgets.hpp"

#include <filesystem>                              // for exists, is_directory
#include <ftxui/component/component.hpp>           // for Renderer, Button
#include <ftxui/component/component_options.hpp>   // for ButtonOption
#include <ftxui/component/screen_interactive.hpp>  // for Component, ScreenI...
#include <ftxui/dom/elements.hpp>                  // for operator|, size
#include <sys/mount.h>                             // for umount

#include <fmt/compile.h>
#include <fmt/core.h>

using namespace std::string_view_literals;
namespace fs = std::filesystem;

namespace utils {

void btrfs_create_subvols([[maybe_unused]] const disk_part& disk, const std::string_view& mode, bool ignore_note) noexcept {
    /* clang-format off */
    if (mode.empty()) { return; }
    /* clang-format on */

#ifdef NDEVENV
    // save mount options and name of the root partition
    utils::exec("mount | grep \"on /mnt \" | grep -Po '(?<=\\().*(?=\\))' > /tmp/.root_mount_options"sv);
    // utils::exec("lsblk -lno MOUNTPOINT,NAME | awk '/^\\/mnt / {print $2}' > /tmp/.root_partition"sv);

    if (mode == "manual"sv) {
        // Create subvolumes manually
        std::string subvols{"@ @home @cache"};
        static constexpr auto subvols_body = "\nInput names of the subvolumes separated by spaces.\nThe first one will be used for mounting /.\n"sv;
        if (!tui::detail::inputbox_widget(subvols, subvols_body, size(ftxui::HEIGHT, ftxui::GREATER_THAN, 4))) {
            return;
        }
        const auto& saved_path = fs::current_path();
        fs::current_path("/mnt");
        auto subvol_list = utils::make_multiline(subvols, false, ' ');
        for (const auto& subvol : subvol_list) {
            utils::exec(fmt::format(FMT_COMPILE("btrfs subvolume create {} 2>>/tmp/cachyos-install.log"), subvol), true);
        }
        fs::current_path(saved_path);
        // Mount subvolumes
        umount("/mnt");
        // Mount the first subvolume as /
        utils::exec(fmt::format(FMT_COMPILE("mount -o {},subvol=\"{}\" \"{}\" /mnt"), disk.mount_opts, subvol_list[0], disk.root));
        // Remove the first subvolume from the subvolume list
        subvol_list.erase(subvol_list.begin());

        // Loop to mount all created subvolumes
        for (const auto& subvol : subvol_list) {
            std::string mountp{"/home"};
            const auto& mountp_body = fmt::format(FMT_COMPILE("\nInput mountpoint of the subvolume {}\nas it would appear in installed system\n(without prepending /mnt).\n"), subvol);
            if (!tui::detail::inputbox_widget(mountp, mountp_body, size(ftxui::HEIGHT, ftxui::GREATER_THAN, 4))) {
                return;
            }

            const auto& mountp_formatted = fmt::format(FMT_COMPILE("/mnt{}"), mountp);
            fs::create_directories(mountp_formatted);
            utils::exec(fmt::format(FMT_COMPILE("mount -o {},subvol=\"{}\" \"{}\" \"{}\""), disk.mount_opts, subvol, disk.root, mountp_formatted));
        }
        return;
    }
    if (!ignore_note) {
        static constexpr auto content = "\nThis creates subvolumes:\n@ for /,\n@home for /home,\n@cache for /var/cache.\n"sv;
        const auto& do_create         = tui::detail::yesno_widget(content, size(ftxui::HEIGHT, ftxui::LESS_THAN, 15) | size(ftxui::WIDTH, ftxui::LESS_THAN, 75));
        /* clang-format off */
        if (!do_create) { return; }
        /* clang-format on */
    }

    // Create subvolumes automatically
    const auto& saved_path = fs::current_path();
    fs::current_path("/mnt");
    utils::exec("btrfs subvolume create @ 2>>/tmp/cachyos-install.log"sv, true);
    utils::exec("btrfs subvolume create @home 2>>/tmp/cachyos-install.log"sv, true);
    utils::exec("btrfs subvolume create @cache 2>>/tmp/cachyos-install.log"sv, true);
    // utils::exec("btrfs subvolume create @snapshots 2>>/tmp/cachyos-install.log"sv, true);
    fs::current_path(saved_path);
    // Mount subvolumes
    umount("/mnt");
    utils::exec(fmt::format(FMT_COMPILE("mount -o {},subvol=@ \"{}\" /mnt"), disk.mount_opts, disk.root));
    fs::create_directories("/mnt/home");
    fs::create_directories("/mnt/var/cache");
    utils::exec(fmt::format(FMT_COMPILE("mount -o {},subvol=@home \"{}\" /mnt/home"), disk.mount_opts, disk.root));
    utils::exec(fmt::format(FMT_COMPILE("mount -o {},subvol=@cache \"{}\" /mnt/var/cache"), disk.mount_opts, disk.root));
#else
    spdlog::info("Do we ignore note? {}", ignore_note);
#endif
}

void mount_existing_subvols(const disk_part& disk) noexcept {
    // Set mount options
    const auto& format_name   = utils::exec(fmt::format(FMT_COMPILE("echo {} | rev | cut -d/ -f1 | rev"), disk.part));
    const auto& format_device = utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/disk/p\" | {}"), format_name, "awk '/disk/ {print $1}'"sv));

    std::string fs_opts{};
    if (utils::exec(fmt::format(FMT_COMPILE("cat /sys/block/{}/queue/rotational)"), format_device), true) == "1"sv) {
        fs_opts = "autodefrag,compress=zlib,noatime,nossd,commit=120"sv;
    } else {
        fs_opts = "compress=lzo,noatime,space_cache,ssd,commit=120"sv;
    }
#ifdef NDEVENV
    utils::exec("btrfs subvolume list /mnt 2>/dev/null | cut -d\" \" -f9 > /tmp/.subvols"sv, true);
    umount("/mnt");

    // Mount subvolumes one by one
    for (const auto& subvol : utils::make_multiline(utils::exec("cat /tmp/.subvols"sv))) {
        // Ask for mountpoint
        const auto& content = fmt::format(FMT_COMPILE("\nInput mountpoint of\nthe subvolume {}\nas it would appear\nin installed system\n(without prepending /mnt).\n"), subvol);
        std::string mountpoint{"/"};
        if (!tui::detail::inputbox_widget(mountpoint, content, size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
            return;
        }
        const auto& mount_dir{fmt::format(FMT_COMPILE("/mnt/{}"), mountpoint)};
        if (!fs::exists(mount_dir)) {
            fs::create_directories(mount_dir);
        }
        // Mount the subvolume
        utils::exec(fmt::format(FMT_COMPILE("mount -o \"{},subvol={}\" \"{}\" \"{}\""), fs_opts, subvol, disk.root, mount_dir));
    }
#endif
}

std::vector<std::string> lvm_show_vg() noexcept {
    const auto& vg_list = utils::make_multiline(utils::exec("lvs --noheadings | awk '{print $2}' | uniq"sv));

    std::vector<std::string> res{};
    res.reserve(vg_list.size());
    for (const auto& vg : vg_list) {
        const auto& temp = utils::exec(fmt::format(FMT_COMPILE("vgdisplay {} | grep -i \"vg size\" | {}"), vg, "awk '{print $3$4}'"sv));
        res.push_back(temp);
    }

    return res;
}

// Automated configuration of zfs. Creates a new zpool and a default set of filesystems
bool zfs_auto_pres(const std::string_view& partition, const std::string_view& zfs_zpool_name) noexcept {
    // first we need to create a zpool to hold the datasets/zvols
    if (!utils::zfs_create_zpool(partition, zfs_zpool_name)) {
        return false;
    }

    // next create the datasets including their parents
    utils::zfs_create_dataset(fmt::format(FMT_COMPILE("{}/ROOT"), zfs_zpool_name), "none"sv);
    utils::zfs_create_dataset(fmt::format(FMT_COMPILE("{}/ROOT/cos"), zfs_zpool_name), "none"sv);
    utils::zfs_create_dataset(fmt::format(FMT_COMPILE("{}/ROOT/cos/root"), zfs_zpool_name), "/"sv);
    utils::zfs_create_dataset(fmt::format(FMT_COMPILE("{}/ROOT/cos/home"), zfs_zpool_name), "/home"sv);
    utils::zfs_create_dataset(fmt::format(FMT_COMPILE("{}/ROOT/cos/varcache"), zfs_zpool_name), "/var/cache"sv);
    utils::zfs_create_dataset(fmt::format(FMT_COMPILE("{}/ROOT/cos/varlog"), zfs_zpool_name), "/var/log"sv);

#ifdef NDEVENV
    // set the rootfs
    utils::exec(fmt::format(FMT_COMPILE("zpool set bootfs={0}/ROOT/cos/root {0} 2>>/tmp/cachyos-install.log"), zfs_zpool_name), true);
#endif
    return true;
}

// creates a new zpool on an existing partition
bool zfs_create_zpool(const std::string_view& partition, const std::string_view& pool_name) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    config_data["ZFS_ZPOOL_NAME"] = std::string{pool_name.data()};

    static constexpr auto zpool_options{"-f -o ashift=12 -o autotrim=on -O acltype=posixacl -O compression=zstd -O atime=off -O relatime=off -O normalization=formD -O xattr=sa -O mountpoint=none"sv};

#ifdef NDEVENV
    std::int32_t ret_code{};

    // Find the UUID of the partition
    const auto& partuuid = utils::exec(fmt::format(FMT_COMPILE("lsblk -lno PATH,PARTUUID | grep \"^{}\" | {}"), partition, "awk '{print $2}'"sv), false);

    // See if the partition has a partuuid, if not use the device name
    const auto& zfs_zpool_cmd = fmt::format(FMT_COMPILE("zpool create {} {}"), zpool_options, pool_name);
    if (!partuuid.empty()) {
        ret_code = utils::to_int(utils::exec(fmt::format(FMT_COMPILE("{} {} 2>>/tmp/cachyos-install.log"), zfs_zpool_cmd, partuuid), true));
        spdlog::info("Creating zpool {} on device {} using partuuid {}", pool_name, partition, partuuid);
    } else {
        ret_code = utils::to_int(utils::exec(fmt::format(FMT_COMPILE("{} {} 2>>/tmp/cachyos-install.log"), zfs_zpool_cmd, partition), true));
        spdlog::info("Creating zpool {} on device {}", pool_name, partition);
    }

    if (ret_code != 0) {
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
    utils::exec(fmt::format(FMT_COMPILE("zpool export {} 2>>/tmp/cachyos-install.log"), pool_name), true);
    utils::exec(fmt::format(FMT_COMPILE("zpool import -R {} {} 2>>/tmp/cachyos-install.log"), mountpoint, pool_name), true);
#endif

    return true;
}

// Creates a zfs volume
void zfs_create_zvol(const std::string_view& zsize, const std::string_view& zpath) noexcept {
#ifdef NDEVENV
    utils::exec(fmt::format(FMT_COMPILE("zfs create -V {}M {} 2>>/tmp/cachyos-install.log"), zsize, zpath), true);
#else
    spdlog::debug("zfs create -V {}M {}", zsize, zpath);
#endif
}

// Creates a zfs filesystem, the first parameter is the ZFS path and the second is the mount path
void zfs_create_dataset(const std::string_view& zpath, const std::string_view& zmount) noexcept {
#ifdef NDEVENV
    utils::exec(fmt::format(FMT_COMPILE("zfs create -o mountpoint={} {} 2>>/tmp/cachyos-install.log"), zmount, zpath), true);
#else
    spdlog::debug("zfs create -o mountpoint={} {}", zmount, zpath);
#endif
}

void zfs_destroy_dataset(const std::string_view& zdataset) noexcept {
#ifdef NDEVENV
    utils::exec(fmt::format(FMT_COMPILE("zfs destroy -r {} 2>>/tmp/cachyos-install.log"), zdataset), true);
#else
    spdlog::debug("zfs destroy -r {}", zdataset);
#endif
}

// returns a list of imported zpools
std::string zfs_list_pools() noexcept {
#ifdef NDEVENV
    return utils::exec("zfs list -H -o name 2>/dev/null | grep \"/\""sv);
#else
    return "vol0\nvol1\n";
#endif
}

// returns a list of devices containing zfs members
std::string zfs_list_devs() noexcept {
    std::string list_of_devices{};
    // get a list of devices with zpools on them
    const auto& devices = utils::make_multiline(utils::exec("zpool status -PL 2>/dev/null | awk '{print $1}' | grep \"^/\""sv));
    for (const auto& device : devices) {
        // add the device
        list_of_devices += fmt::format(FMT_COMPILE("{}\n"), device);
        // now let's add any other forms of those devices
        list_of_devices += utils::exec(fmt::format(FMT_COMPILE("find -L /dev/ -xtype l -samefile {} 2>/dev/null"), device));
    }
    return list_of_devices;
}

std::string zfs_list_datasets(const std::string_view& type) noexcept {
#ifdef NDEVENV
    if (type == "zvol"sv) {
        return utils::exec("zfs list -Ht volume -o name,volsize 2>/dev/null"sv);
    } else if (type == "legacy"sv) {
        return utils::exec("zfs list -Ht filesystem -o name,mountpoint 2>/dev/null | grep \"^.*/.*legacy$\" | awk '{print $1}'"sv);
    }

    return utils::exec("zfs list -H -o name 2>/dev/null | grep \"/\""sv);
#else
    spdlog::debug("type := {}", type);
    return "zpcachyos";
#endif
}

void zfs_set_property(const std::string_view& property, const std::string_view& dataset) noexcept {
#ifdef NDEVENV
    utils::exec(fmt::format(FMT_COMPILE("zfs set {} {} 2>>/tmp/cachyos-install.log"), property, dataset), true);
#else
    spdlog::debug("zfs set {} {}", property, dataset);
#endif
}

// Other filesystems
void select_filesystem(const std::string_view& file_sys) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    config_data["FILESYSTEM_NAME"] = std::string{file_sys.data()};

    if (file_sys == "btrfs"sv) {
        config_data["FILESYSTEM"] = "mkfs.btrfs -f";
        config_data["fs_opts"]    = std::vector<std::string>{"autodefrag", "compress=zlib", "compress=lzo", "compress=zstd", "compress=no", "compress-force=zlib", "compress-force=lzo", "compress-force=zstd", "discard", "noacl", "noatime", "nodatasum", "nospace_cache", "recovery", "skip_balance", "space_cache", "nossd", "ssd", "ssd_spread", "commit=120"};
#ifdef NDEVENV
        utils::exec("modprobe btrfs"sv);
#endif
    } else if (file_sys == "ext4"sv) {
        config_data["FILESYSTEM"] = "mkfs.ext4 -q";
        config_data["fs_opts"]    = std::vector<std::string>{"data=journal", "data=writeback", "dealloc", "discard", "noacl", "noatime", "nobarrier", "nodelalloc"};
    } else if (file_sys == "f2fs"sv) {
        config_data["FILESYSTEM"] = "mkfs.f2fs -q";
        config_data["fs_opts"]    = std::vector<std::string>{"data_flush", "disable_roll_forward", "disable_ext_identify", "discard", "fastboot", "flush_merge", "inline_xattr", "inline_data", "inline_dentry", "no_heap", "noacl", "nobarrier", "noextent_cache", "noinline_data", "norecovery"};
#ifdef NDEVENV
        utils::exec("modprobe f2fs"sv);
#endif
    } else if (file_sys == "xfs"sv) {
        config_data["FILESYSTEM"] = "mkfs.xfs -f";
        config_data["fs_opts"]    = std::vector<std::string>{"discard", "filestreams", "ikeep", "largeio", "noalign", "nobarrier", "norecovery", "noquota", "wsync"};
    } else if (file_sys != "zfs"sv) {
        spdlog::error("Invalid filesystem ('{}')!", file_sys);
    }
}

}  // namespace utils
