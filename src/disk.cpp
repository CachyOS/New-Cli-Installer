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
