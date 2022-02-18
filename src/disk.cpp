#include "disk.hpp"
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

namespace fs = std::filesystem;

namespace utils {

void btrfs_create_subvols([[maybe_unused]] const disk_part& disk, const std::string_view& mode) noexcept {
    /* clang-format off */
    if (mode.empty()) { return; }
    /* clang-format on */

#ifdef NDEVENV
    // save mount options and name of the root partition
    utils::exec("mount | grep \"on /mnt \" | grep -Po '(?<=\\().*(?=\\))' > /tmp/.root_mount_options");
    // utils::exec("lsblk -lno MOUNTPOINT,NAME | awk '/^\\/mnt / {print $2}' > /tmp/.root_partition");

    if (mode == "manual") {
        // Create subvolumes manually
        std::string subvols{"@ @home @cache"};
        static constexpr auto subvols_body = "\nInput names of the subvolumes separated by spaces.\nThe first one will be used for mounting /.\n";
        if (!tui::detail::inputbox_widget(subvols, subvols_body, size(ftxui::HEIGHT, ftxui::GREATER_THAN, 4))) {
            return;
        }
        const auto& saved_path = fs::current_path();
        fs::current_path("/mnt");
        auto subvol_list = utils::make_multiline(subvols, false, " ");
        for (const auto& subvol : subvol_list) {
            utils::exec(fmt::format(FMT_COMPILE("btrfs subvolume create {}"), subvol), true);
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
    static constexpr auto content = "\nThis creates subvolumes:\n@ for /,\n@home for /home,\n@cache for /var/cache.\n";
    const auto& do_create         = tui::detail::yesno_widget(content, size(ftxui::HEIGHT, ftxui::LESS_THAN, 15) | size(ftxui::WIDTH, ftxui::LESS_THAN, 75));
    /* clang-format off */
    if (!do_create) { return; }
    /* clang-format on */

    // Create subvolumes automatically
    const auto& saved_path = fs::current_path();
    fs::current_path("/mnt");
    utils::exec("btrfs subvolume create @", true);
    utils::exec("btrfs subvolume create @home", true);
    utils::exec("btrfs subvolume create @cache", true);
    // utils::exec("btrfs subvolume create @snapshots", true);
    fs::current_path(saved_path);
    // Mount subvolumes
    umount("/mnt");
    utils::exec(fmt::format(FMT_COMPILE("mount -o {},subvol=@ \"{}\" /mnt"), disk.mount_opts, disk.root));
    fs::create_directories("/mnt/home");
    fs::create_directories("/mnt/var/cache");
    utils::exec(fmt::format(FMT_COMPILE("mount -o {},subvol=@home \"{}\" /mnt/home"), disk.mount_opts, disk.root));
    utils::exec(fmt::format(FMT_COMPILE("mount -o {},subvol=@cache \"{}\" /mnt/var/cache"), disk.mount_opts, disk.root));
#endif
}

void mount_existing_subvols(const disk_part& disk) noexcept {
    // Set mount options
    const auto& format_name   = utils::exec(fmt::format(FMT_COMPILE("echo {} | rev | cut -d/ -f1 | rev"), disk.part));
    const auto& format_device = utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/disk/p\" | {}"), format_name, "awk '/disk/ {print $1}'"));

    std::string fs_opts{};
    if (utils::exec(fmt::format(FMT_COMPILE("cat /sys/block/{}/queue/rotational)"), format_device), true) == "1") {
        fs_opts = "autodefrag,compress=zlib,noatime,nossd,commit=120";
    } else {
        fs_opts = "compress=lzo,noatime,space_cache,ssd,commit=120";
    }
#ifdef NDEVENV
    utils::exec("btrfs subvolume list /mnt | cut -d\" \" -f9 > /tmp/.subvols", true);
    umount("/mnt");

    // Mount subvolumes one by one
    for (const auto& subvol : utils::make_multiline(utils::exec("cat /tmp/.subvols"))) {
        // Ask for mountpoint
        const auto& content = fmt::format(FMT_COMPILE("\nInput mountpoint of the subvolume {}\nas it would appear in installed system\n(without prepending /mnt).\n"), subvol);
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

}  // namespace utils
