#include "tui.hpp"
#include "config.hpp"
#include "definitions.hpp"
#include "utils.hpp"
#include "widgets.hpp"

/* clang-format off */
#include <sys/mount.h>                             // for mount
#include <csignal>                                 // for raise
#include <algorithm>                               // for transform
#include <filesystem>                              // for exists, is_directory
#include <memory>                                  // for __shared_ptr_access
#include <regex>                                   // for regex_search, match_results<>::_Base_type
#include <string>                                  // for basic_string
#include <ftxui/component/captured_mouse.hpp>      // for ftxui
#include <ftxui/component/component.hpp>           // for Renderer, Button
#include <ftxui/component/component_options.hpp>   // for ButtonOption
#include <ftxui/component/screen_interactive.hpp>  // for Component, ScreenI...
#include <ftxui/dom/elements.hpp>                  // for operator|, size
/* clang-format on */

using namespace ftxui;
namespace fs = std::filesystem;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"

#include <range/v3/algorithm/copy.hpp>

#pragma clang diagnostic pop
#else
namespace ranges = std::ranges;
#endif

namespace tui {

// Revised to deal with partion sizes now being displayed to the user
bool confirm_mount([[maybe_unused]] const std::string_view& part_user) {
#ifdef NDEVENV
    const auto& ret_status = utils::exec(fmt::format("mount | grep {}", part_user), true);
    if (ret_status != "0") {
        detail::infobox_widget("\nMount Failed!\n");
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return false;
    }
#endif
    auto* config_instance   = Config::instance();
    auto& config_data       = config_instance->data();
    const auto& partition   = std::get<std::string>(config_data["PARTITION"]);
    auto& partitions        = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);
    auto& number_partitions = std::get<std::int32_t>(config_data["NUMBER_PARTITIONS"]);

    detail::infobox_widget("\nMount Successful!\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // TODO: reimplement natively
    const auto& str      = utils::make_multiline(partitions);
    const auto& cmd      = fmt::format("echo \"{0}\" | sed \"s~{1} [0-9]*[G-M]~~\" | sed \"s~{1} [0-9]*\\.[0-9]*[G-M]~~\" | sed \"s~{1}$\' -\'~~\"", str, partition);
    const auto& res_text = utils::exec(cmd);
    partitions           = utils::make_multiline(res_text);
    number_partitions -= 1;
    return true;
}

// Fsck hook
void set_fsck_hook() noexcept {
    auto* config_instance    = Config::instance();
    auto& config_data        = config_instance->data();
    const auto& do_sethook   = detail::yesno_widget("\nDo you want to use fsck hook?\n", size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    config_data["FSCK_HOOK"] = do_sethook;
}

void install_cust_pkgs() noexcept {
    std::string packages{};
    static constexpr auto content = "\nType any extra packages you would like to add, separated by spaces.\n\nFor example, to install Firefox, MPV, FZF:\nfirefox mpv fzf\n";
    if (!detail::inputbox_widget(packages, content, size(HEIGHT, GREATER_THAN, 4))) {
        return;
    }
    // If at least one package, install.
    /* clang-format off */
    if (packages.empty()) { return; }
    /* clang-format on */

#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& hostcache  = std::get<std::int32_t>(config_data["hostcache"]);

    if (hostcache) {
        utils::exec(fmt::format("pacstrap {} {}", mountpoint, packages));
        return;
    }
    utils::exec(fmt::format("pacstrap -c {} {}", mountpoint, packages));
#endif
}

void rm_pgs() noexcept {
    std::string packages{};
    static constexpr auto content = "\nType any packages you would like to remove, separated by spaces.\n\nFor example, to remove Firefox, MPV, FZF:\nfirefox mpv fzf\n";
    if (!detail::inputbox_widget(packages, content, size(HEIGHT, GREATER_THAN, 4))) {
        return;
    }
    /* clang-format off */
    if (packages.empty()) { return; }
    /* clang-format on */

#ifdef NDEVENV
    utils::exec(fmt::format("arch-chroot \"pacman -Rsn {}\"", packages));
#endif
}

void install_systemd_boot() noexcept {
    static constexpr auto content = "\nThis installs systemd-boot and generates boot entries\nfor the currently installed kernels.\nThis bootloader requires your kernels to be on the UEFI partition.\nThis is achieved by mounting the UEFI partition to /boot.\n";
    const auto& do_install_uefi   = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    /* clang-format off */
    if (!do_install_uefi) { return; }
    /* clang-format on */

#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& uefi_mount = std::get<std::string>(config_data["UEFI_MOUNT"]);

    utils::exec(fmt::format("arch-chroot \"bootctl --path={} install\"", uefi_mount));
    utils::exec(fmt::format("pacstrap {} systemd-boot-manager", mountpoint));
    utils::exec("arch-chroot \"sdboot-manage gen\"");

    // Check if the volume is removable. If so, dont use autodetect
    const auto& root_name   = utils::exec("mount | awk \'/\\/mnt / {print $1}\' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    const auto& root_device = utils::exec(fmt::format("lsblk -i | tac | sed -r \'s/^[^[:alnum:]]+//\' | sed -n -e \"/{}/,/disk/p\" | {}", root_name, "awk \'/disk/ {print $1}\'"));
    spdlog::info("root_name: {}. root_device: {}", root_name, root_device);
    const auto& removable = utils::exec(fmt::format("cat /sys/block/{}/removable", root_device));
    if (utils::to_int(removable.data()) == 1) {
        // Remove autodetect hook
        utils::exec("sed -i -e \'/^HOOKS=/s/\\ autodetect//g\' /mnt/etc/mkinitcpio.conf");
        spdlog::info("\"Autodetect\" hook was removed");
    }
#endif
    spdlog::info("Systemd-boot was installed");
    detail::infobox_widget("\nSystemd-boot was installed\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

void uefi_bootloader() noexcept {
    // Ensure again that efivarfs is mounted
    static constexpr auto efi_path = "/sys/firmware/efi/";
    if (fs::exists(efi_path) && fs::is_directory(efi_path)) {
        // Mount efivarfs if it is not already mounted
        const auto& mount_out = utils::exec("mount | grep /sys/firmware/efi/efivars");
        if (mount_out.empty()) {
            if (mount("efivarfs", "/sys/firmware/efi/efivars", "efivarfs", 0, "") != 0) {
                perror("utils::uefi_bootloader");
                exit(1);
            }
        }
    }

    static constexpr auto bootloaderInfo  = "Refind can be used standalone or in conjunction with other bootloaders as a graphical bootmenu.\nIt autodetects all bootable systems at boot time.\nGrub supports encrypted /boot partition and detects all bootable systems when you update your kernels.\nIt supports booting .iso files from a harddrive and automatic boot entries for btrfs snapshots.\nSystemd-boot is very light and simple and has little automation.\nIt autodetects windows, but is otherwise unsuited for multibooting.";
    std::vector<std::string> menu_entries = {
        "grub",
        "refind",
        "systemd-boot",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            // tui::install_grub_uefi();
            spdlog::debug("grub");
            break;
        case 1:
            // tui::install_refind();
            spdlog::debug("refind");
            break;
        case 2:
            tui::install_systemd_boot();
            break;
        }
        std::raise(SIGINT);
    };

    MenuOption menu_option{.on_enter = ok_callback};
    auto menu    = Menu(&menu_entries, &selected, &menu_option);
    auto content = Renderer(menu, [&] {
        return menu->Render() | center | size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    });

    ButtonOption button_option{.border = false};
    auto controls_container = detail::controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    auto global = Container::Vertical({
        Renderer([] { return detail::multiline_text(utils::make_multiline(bootloaderInfo)) | size(HEIGHT, GREATER_THAN, 5); }),
        Renderer([] { return separator(); }),
        content,
        Renderer([] { return separator(); }),
        controls,
    });

    auto renderer = Renderer(global, [&] {
        return detail::centered_interative_multi("New CLI Installer", global);
    });

    screen.Loop(renderer);
}

void install_base() noexcept {
    static constexpr auto base_installed = "/mnt/.base_installed";
    if (fs::exists(base_installed)) {
        static constexpr auto content = "\nA CachyOS Base has already been installed on this partition.\nProceed anyway?\n";
        const auto& do_reinstall      = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
        /* clang-format off */
        if (!do_reinstall) { return; }
        /* clang-format on */
        fs::remove(base_installed);
    }
    // Prep variables
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const std::vector<std::string> available_kernels{"linux-cachyos", "linux", "linux-zen", "linux-lts", "linux-cachyos-cacule", "linux-cachyos-cacule-rdb", "linux-cachyos-bmq", "linux-cachyos-pds", "linux-cachyos-baby", "linux-cachyos-cacule-lts"};

    // Create the base list of packages
    std::vector<std::string> install_packages{};

    std::unique_ptr<bool[]> kernels_state{new bool[available_kernels.size()]{false}};

    auto kernels = Container::Vertical(detail::from_vector_checklist(available_kernels, kernels_state.get()));

    auto screen  = ScreenInteractive::Fullscreen();
    auto content = Renderer(kernels, [&] {
        return kernels->Render() | center | size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40) | vscroll_indicator;
    });

    auto ok_callback = [&] {
        const auto& packages = detail::from_checklist_string(available_kernels, kernels_state.get());
        auto ret_status      = utils::exec(fmt::format("grep \"linux\" {}", packages), true);
        if (ret_status == "0") {
            // Check if a kernel is already installed
            ret_status = utils::exec(fmt::format("ls {}/boot/*.img >/dev/null 2>&1", mountpoint), true);
            if (ret_status != "0") {
                static constexpr auto ErrNoKernel = "\nAt least one kernel must be selected.\n";
                detail::msgbox_widget(ErrNoKernel);
                return;
            }
            const auto& cmd = utils::exec(fmt::format("ls {}/boot/*.img | cut -d'-' -f2 | grep -v ucode.img | sort -u", mountpoint));
            detail::msgbox_widget(fmt::format("\nlinux-{} detected\n", cmd));
            std::raise(SIGINT);
        }
        std::raise(SIGINT);
    };

    ButtonOption button_option{.border = false};
    auto controls_container = detail::controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    static constexpr auto InstStandBseBody = "\nThe base package group will be installed automatically.\nThe base-devel package group is required to use the Arch User Repository (AUR).\n";
    static constexpr auto UseSpaceBar      = "Use [Spacebar] to de/select options listed.";
    const auto& kernels_options_body       = fmt::format("\n{}{}\n", InstStandBseBody, UseSpaceBar);
    auto global                            = Container::Vertical({
        Renderer([&] { return detail::multiline_text(utils::make_multiline(kernels_options_body)); }),
        Renderer([] { return separator(); }),
        content,
        Renderer([] { return separator(); }),
        controls,
    });

    auto renderer = Renderer(global, [&] {
        const auto& title = "New CLI Installer | Install Base";
        return detail::centered_interative_multi(title, global);
    });

    screen.Loop(renderer);

    // filter_packages
    // utils::exec(fmt::format("pacstrap ${MOUNTPOINT} $(cat /mnt/.base) |& tee /tmp/pacstrap.log"));

    // std::ofstream(base_installed);
}

void bios_bootloader() { }

void install_bootloader() {
    /* clang-format off */
    if (!utils::check_base()) { return; }
    /* clang-format on */

    auto* config_instance   = Config::instance();
    auto& config_data       = config_instance->data();
    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    if (system_info == "BIOS")
        bios_bootloader();
    else
        uefi_bootloader();
}

// BIOS and UEFI
void auto_partition() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& device_info                  = std::get<std::string>(config_data["DEVICE"]);
    [[maybe_unused]] const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);

    // Find existing partitions (if any) to remove
    auto parts            = utils::exec(fmt::format("parted -s {} print | {}", device_info, "awk \'/^ / {print $1}\'"));
    const auto& del_parts = utils::make_multiline(parts);
    for (const auto& del_part : del_parts) {
#ifdef NDEVENV
        utils::exec(fmt::format("parted -s {} rm {} >/dev/null", device_info, del_part));
#else
        spdlog::debug("{}", del_part);
#endif
    }

#ifdef NDEVENV
    // Identify the partition table
    const auto& part_table = utils::exec(fmt::format("parted -s {} print | grep -i \'partition table\' | {}", device_info, "awk \'{print $3}\'"));

    // Create partition table if one does not already exist
    if ((system_info == "BIOS") && (part_table != "msdos"))
        utils::exec(fmt::format("parted -s {} mklabel msdos >/dev/null", device_info));
    if ((system_info == "UEFI") && (part_table != "gpt"))
        utils::exec(fmt::format("parted -s {} mklabel gpt >/dev/null", device_info));

    // Create partitions (same basic partitioning scheme for BIOS and UEFI)
    if (system_info == "BIOS")
        utils::exec(fmt::format("parted -s {} mkpart primary ext3 1MiB 513MiB >/dev/null", device_info));
    else
        utils::exec(fmt::format("parted -s {} mkpart ESP fat32 1MiB 513MiB >/dev/null", device_info));

    utils::exec(fmt::format("parted -s {} set 1 boot on >/dev/null", device_info));
    utils::exec(fmt::format("parted -s {} mkpart primary ext3 513MiB 100% >/dev/null", device_info));
#endif

    // Show created partitions
    auto disklist = utils::exec(fmt::format("lsblk {} -o NAME,TYPE,FSTYPE,SIZE", device_info));

    auto screen = ScreenInteractive::Fullscreen();
    /* clang-format off */
    auto button_option   = ButtonOption();
    button_option.border = false;
    auto button_back     = Button("Back", screen.ExitLoopClosure(), &button_option);

    auto container = Container::Horizontal({button_back});
    auto renderer  = Renderer(container, [&] {
        return detail::centered_widget(container, "New CLI Installer", detail::multiline_text(utils::make_multiline(disklist)) | size(HEIGHT, GREATER_THAN, 5));
    });
    /* clang-format on */

    screen.Loop(renderer);
}

// Simple code to show devices / partitions.
void show_devices() noexcept {
    auto screen = ScreenInteractive::Fullscreen();
    auto lsblk  = utils::exec("lsblk -o NAME,MODEL,TYPE,FSTYPE,SIZE,MOUNTPOINT | grep \"disk\\|part\\|lvm\\|crypt\\|NAME\\|MODEL\\|TYPE\\|FSTYPE\\|SIZE\\|MOUNTPOINT\"");

    /* clang-format off */
    auto button_option   = ButtonOption();
    button_option.border = false;
    auto button_back     = Button("Back", screen.ExitLoopClosure(), &button_option);

    auto container = Container::Horizontal({button_back});
    auto renderer  = Renderer(container, [&] {
        return detail::centered_widget(container, "New CLI Installer", detail::multiline_text(utils::make_multiline(lsblk)) | size(HEIGHT, GREATER_THAN, 5));
    });
    /* clang-format on */

    screen.Loop(renderer);
}

// This function does not assume that the formatted device is the Root installation device as
// more than one device may be formatted. Root is set in the mount_partitions function.
bool select_device() noexcept {
    auto* config_instance    = Config::instance();
    auto& config_data        = config_instance->data();
    auto devices             = utils::exec("lsblk -lno NAME,SIZE,TYPE | grep 'disk' | awk '{print \"/dev/\" $1 \" \" $2}' | sort -u");
    const auto& devices_list = utils::make_multiline(devices);

    std::int32_t selected{};
    bool success{};
    auto ok_callback = [&] {
        auto src              = devices_list[static_cast<std::size_t>(selected)];
        const auto& lines     = utils::make_multiline(src, false, " ");
        config_data["DEVICE"] = lines[0];
        success               = true;
        std::raise(SIGINT);
    };
    detail::menu_widget(devices_list, ok_callback, &selected);

    return success;
}

// Set static list of filesystems rather than on-the-fly. Partially as most require additional flags, and
// partially because some don't seem to be viable.
// Set static list of filesystems rather than on-the-fly.
bool select_filesystem() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    // prep variables
    config_data["fs_opts"] = std::vector<std::string>{};

    std::vector<std::string> menu_entries = {
        "Do not format -",
        "btrfs mkfs.btrfs -f",
        "ext2 mkfs.ext2 -q",
        "ext3 mkfs.ext3 -q",
        "ext4 mkfs.ext4 -q",
        "f2fs mkfs.f2fs -q",
        "jfs mkfs.jfs -q",
        "nilfs2 mkfs.nilfs2 -fq",
        "ntfs mkfs.ntfs -q",
        "reiserfs mkfs.reiserfs -q",
        "vfat mkfs.vfat -F32",
        "xfs mkfs.xfs -f",
    };

    std::int32_t selected{};
    bool success{};
    auto ok_callback = [&] {
        auto src             = menu_entries[static_cast<std::size_t>(selected)];
        const auto& lines    = utils::make_multiline(src, false, " ");
        const auto& file_sys = lines[0];
        if (file_sys == "btrfs") {
            config_data["FILESYSTEM"] = "mkfs.btrfs -f";
            config_data["fs_opts"]    = std::vector<std::string>{"autodefrag", "compress=zlib", "compress=lzo", "compress=zstd", "compress=no", "compress-force=zlib", "compress-force=lzo", "compress-force=zstd", "discard", "noacl", "noatime", "nodatasum", "nospace_cache", "recovery", "skip_balance", "space_cache", "nossd", "ssd", "ssd_spread", "commit=120"};
#ifdef NDEVENV
            utils::exec("modprobe btrfs");
#endif
        } else if (file_sys == "ext2") {
            config_data["FILESYSTEM"] = "mkfs.ext2 -q";
        } else if (file_sys == "ext3") {
            config_data["FILESYSTEM"] = "mkfs.ext3 -q";
        } else if (file_sys == "ext4") {
            config_data["FILESYSTEM"] = "mkfs.ext4 -q";
            config_data["fs_opts"]    = std::vector<std::string>{"data=journal", "data=writeback", "dealloc", "discard", "noacl", "noatime", "nobarrier", "nodelalloc"};
        } else if (file_sys == "f2fs") {
            config_data["FILESYSTEM"] = "mkfs.f2fs -q";
            config_data["fs_opts"]    = std::vector<std::string>{"data_flush", "disable_roll_forward", "disable_ext_identify", "discard", "fastboot", "flush_merge", "inline_xattr", "inline_data", "inline_dentry", "no_heap", "noacl", "nobarrier", "noextent_cache", "noinline_data", "norecovery"};
#ifdef NDEVENV
            utils::exec("modprobe f2fs");
#endif
        } else if (file_sys == "jfs") {
            config_data["FILESYSTEM"] = "mkfs.jfs -q";
            config_data["fs_opts"]    = std::vector<std::string>{"discard", "errors=continue", "errors=panic", "nointegrity"};
        } else if (file_sys == "nilfs2") {
            config_data["FILESYSTEM"] = "mkfs.nilfs2 -fq";
            config_data["fs_opts"]    = std::vector<std::string>{"discard", "nobarrier", "errors=continue", "errors=panic", "order=relaxed", "order=strict", "norecovery"};
        } else if (file_sys == "ntfs") {
            config_data["FILESYSTEM"] = "mkfs.ntfs -q";
        } else if (file_sys == "reiserfs") {
            config_data["FILESYSTEM"] = "mkfs.reiserfs -q";
            config_data["fs_opts"]    = std::vector<std::string>{"acl", "nolog", "notail", "replayonly", "user_xattr"};
        } else if (file_sys == "vfat") {
            config_data["FILESYSTEM"] = "mkfs.vfat -F32";
        } else if (file_sys == "xfs") {
            config_data["FILESYSTEM"] = "mkfs.xfs -f";
            config_data["fs_opts"]    = std::vector<std::string>{"discard", "filestreams", "ikeep", "largeio", "noalign", "nobarrier", "norecovery", "noquota", "wsync"};
        }
        success = true;
        std::raise(SIGINT);
    };
    detail::menu_widget(menu_entries, ok_callback, &selected);

    /* clang-format off */
    if (!success) { return false; }
    /* clang-format on */

    // Warn about formatting!
    const auto& file_sys  = std::get<std::string>(config_data["FILESYSTEM"]);
    const auto& partition = std::get<std::string>(config_data["PARTITION"]);
    const auto& content   = fmt::format("\nMount {}\n\n! Data on {} will be lost !\n", file_sys, partition);
    const auto& do_mount  = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    if (do_mount) {
#ifdef NDEVENV
        utils::exec(fmt::format("{} {}", file_sys, partition));
#endif
        spdlog::info("mount.{} {}", partition, file_sys);
    }

    return success;
}

// This subfunction allows for special mounting options to be applied for relevant fs's.
// Seperate subfunction for neatness.
void mount_opts() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& file_sys      = std::get<std::string>(config_data["FILESYSTEM"]);
    const auto& fs_opts       = std::get<std::vector<std::string>>(config_data["fs_opts"]);
    const auto& partition     = std::get<std::string>(config_data["PARTITION"]);
    const auto& format_name   = utils::exec(fmt::format("echo {} | rev | cut -d/ -f1 | rev", partition));
    const auto& format_device = utils::exec(fmt::format("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/disk/p\" | {}", format_name, "awk \'/disk/ {print $1}\'"));

    const auto& rotational_queue = (utils::exec(fmt::format("cat /sys/block/{}/queue/rotational", format_device)) == "1");

    std::unique_ptr<bool[]> fs_opts_state{new bool[fs_opts.size()]{false}};
    for (size_t i = 0; i < fs_opts.size(); ++i) {
        const auto& fs_opt = fs_opts[i];
        auto& fs_opt_state = fs_opts_state[i];
        if (rotational_queue) {
            fs_opt_state = ((fs_opt == "autodefrag")
                || (fs_opt == "compress=zlip")
                || (fs_opt == "nossd"));
        } else {
            fs_opt_state = ((fs_opt == "compress=lzo")
                || (fs_opt == "space_cache")
                || (fs_opt == "commit=120")
                || (fs_opt == "ssd"));
        }

        /* clang-format off */
        if (!fs_opt_state) { fs_opt_state = (fs_opt == "noatime"); }
        /* clang-format on */
    }

    auto flags = Container::Vertical(detail::from_vector_checklist(fs_opts, fs_opts_state.get()));

    auto screen  = ScreenInteractive::Fullscreen();
    auto content = Renderer(flags, [&] {
        return flags->Render() | center | size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40) | vscroll_indicator;
    });

    auto& mount_opts_info = std::get<std::string>(config_data["MOUNT_OPTS"]);
    auto ok_callback      = [&] {
        mount_opts_info = detail::from_checklist_string(fs_opts, fs_opts_state.get());
        std::raise(SIGINT);
    };

    ButtonOption button_option{.border = false};
    auto controls_container = detail::controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    std::string mount_options_body = "\nUse [Space] to de/select the desired mount\noptions and review carefully. Please do not\nselect multiple versions of the same option.\n";
    auto global                    = Container::Vertical({
        Renderer([&] { return detail::multiline_text(utils::make_multiline(mount_options_body)); }),
        Renderer([] { return separator(); }),
        content,
        Renderer([] { return separator(); }),
        controls,
    });

    auto renderer = Renderer(global, [&] {
        const auto& file_sys_formated = utils::exec(fmt::format("echo {} | sed \"s/.*\\.//g;s/-.*//g\"", file_sys));
        const auto& title             = fmt::format("New CLI Installer | {}", file_sys_formated);
        return detail::centered_interative_multi(title, global);
    });

    screen.Loop(renderer);

    // Now clean up the file
    mount_opts_info = utils::exec(fmt::format("echo \"{}\" | sed \'s/ /,/g\'", mount_opts_info));
    mount_opts_info = utils::exec(fmt::format("echo \"{}\" | sed \'$s/,$//\'", mount_opts_info));

    // If mount options selected, confirm choice
    if (!mount_opts_info.empty()) {
        auto confirm_text    = Container::Vertical({
            Renderer([] { return paragraphAlignLeft("Confirm the following mount options:"); }),
            Renderer([&] { return text(mount_opts_info) | dim; }),
        });
        const auto& do_mount = detail::yesno_widget(confirm_text, size(HEIGHT, LESS_THAN, 10) | size(WIDTH, LESS_THAN, 75));
        /* clang-format off */
        if (!do_mount) { mount_opts_info = ""; }
        /* clang-format on */
    }
}

bool mount_current_partition() noexcept {
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& mount_dev  = std::get<std::string>(config_data["MOUNT"]);

#ifdef NDEVENV
    // Make the mount directory
    fs::path mount_dir(fmt::format("{}{}", mountpoint, mount_dev));
    fs::create_directories(mount_dir);
#endif

    config_data["MOUNT_OPTS"] = "";
    /* clang-format off */
    // Get mounting options for appropriate filesystems
    const auto& fs_opts = std::get<std::vector<std::string>>(config_data["fs_opts"]);
    if (!fs_opts.empty()) { mount_opts(); }
    /* clang-format on */

    // TODO: use libmount instead.
    // see https://github.com/util-linux/util-linux/blob/master/sys-utils/mount.c#L734
#ifdef NDEVENV
    const auto& partition       = std::get<std::string>(config_data["PARTITION"]);
    const auto& mount_opts_info = std::get<std::string>(config_data["MOUNT_OPTS"]);
    if (!mount_opts_info.empty()) {
        // check_for_error "mount ${PARTITION} $(cat ${MOUNT_OPTS})"
        const auto& mount_status = utils::exec(fmt::format("mount -o {} {} {}{}", mount_opts_info, partition, mountpoint, mount_dev));
        spdlog::info("{}", mount_status);
    } else {
        // check_for_error "mount ${PARTITION}"
        const auto& mount_status = utils::exec(fmt::format("mount {} {}{}", partition, mountpoint, mount_dev));
        spdlog::info("{}", mount_status);
    }
#endif
    confirm_mount(fmt::format("{}{}", mountpoint, mount_dev));
    /*
    // Identify if mounted partition is type "crypt" (LUKS on LVM, or LUKS alone)
    if [[ $(lsblk -lno TYPE ${PARTITION} | grep "crypt") != "" ]]; then
        // cryptname for bootloader configuration either way
        LUKS=1
        LUKS_NAME=$(echo ${PARTITION} | sed "s~^/dev/mapper/~~g")

        # Check if LUKS on LVM (parent = lvm /dev/mapper/...)
        cryptparts=$(lsblk -lno NAME,FSTYPE,TYPE | grep "lvm" | grep -i "crypto_luks" | uniq | awk '{print "/dev/mapper/"$1}')
        for i in ${cryptparts}; do
            if [[ $(lsblk -lno NAME ${i} | grep $LUKS_NAME) != "" ]]; then
                LUKS_DEV="$LUKS_DEV cryptdevice=${i}:$LUKS_NAME"
                LVM=1
                return 0;
            fi
        done

        // Check if LVM on LUKS
        cryptparts=$(lsblk -lno NAME,FSTYPE,TYPE | grep " crypt$" | grep -i "LVM2_member" | uniq | awk '{print "/dev/mapper/"$1}')
        for i in ${cryptparts}; do
            if [[ $(lsblk -lno NAME ${i} | grep $LUKS_NAME) != "" ]]; then
                LUKS_DEV="$LUKS_DEV cryptdevice=${i}:$LUKS_NAME"
                LVM=1
                return 0;
            fi
        done

        // Check if LUKS alone (parent = part /dev/...)
        cryptparts=$(lsblk -lno NAME,FSTYPE,TYPE | grep "part" | grep -i "crypto_luks" | uniq | awk '{print "/dev/"$1}')
        for i in ${cryptparts}; do
            if [[ $(lsblk -lno NAME ${i} | grep $LUKS_NAME) != "" ]]; then
                LUKS_UUID=$(lsblk -lno UUID,TYPE,FSTYPE ${i} | grep "part" | grep -i "crypto_luks" | awk '{print $1}')
                LUKS_DEV="$LUKS_DEV cryptdevice=UUID=$LUKS_UUID:$LUKS_NAME"
                return 0;
            fi
        done

        // If LVM logical volume....
    elif [[ $(lsblk -lno TYPE ${PARTITION} | grep "lvm") != "" ]]; then
        LVM=1

        // First get crypt name (code above would get lv name)
        cryptparts=$(lsblk -lno NAME,TYPE,FSTYPE | grep "crypt" | grep -i "lvm2_member" | uniq | awk '{print "/dev/mapper/"$1}')
        for i in ${cryptparts}; do
            if [[ $(lsblk -lno NAME ${i} | grep $(echo $PARTITION | sed "s~^/dev/mapper/~~g")) != "" ]]; then
                LUKS_NAME=$(echo ${i} | sed s~/dev/mapper/~~g)
                return 0;
            fi
        done

        // Now get the device (/dev/...) for the crypt name
        cryptparts=$(lsblk -lno NAME,FSTYPE,TYPE | grep "part" | grep -i "crypto_luks" | uniq | awk '{print "/dev/"$1}')
        for i in ${cryptparts}; do
            if [[ $(lsblk -lno NAME ${i} | grep $LUKS_NAME) != "" ]]; then
                # Create UUID for comparison
                LUKS_UUID=$(lsblk -lno UUID,TYPE,FSTYPE ${i} | grep "part" | grep -i "crypto_luks" | awk '{print $1}')

                # Check if not already added as a LUKS DEVICE (i.e. multiple LVs on one crypt). If not, add.
                if [[ $(echo $LUKS_DEV | grep $LUKS_UUID) == "" ]]; then
                    LUKS_DEV="$LUKS_DEV cryptdevice=UUID=$LUKS_UUID:$LUKS_NAME"
                    LUKS=1
                fi

                return 0;
            fi
        done
    fi*/
    return true;
}

void make_swap() noexcept {
    static constexpr auto SelSwpNone = "None";
    static constexpr auto SelSwpFile = "Swapfile";

    auto* config_instance       = Config::instance();
    auto& config_data           = config_instance->data();
    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);

    {
        std::vector<std::string> temp{"None -"};
        const auto& root_filesystem = utils::exec(fmt::format("findmnt -ln -o FSTYPE \"{}\"", mountpoint_info));
        if (!(root_filesystem == "zfs" || root_filesystem == "btrfs")) {
            temp.push_back("Swapfile -");
        }
        const auto& partitions = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);
        temp.reserve(partitions.size());
        ranges::copy(partitions, std::back_inserter(temp));

        std::int32_t selected{};
        bool success{};
        auto ok_callback = [&] {
            auto src              = temp[static_cast<std::size_t>(selected)];
            const auto& lines     = utils::make_multiline(src, false, " ");
            config_data["ANSWER"] = lines[0];
            success               = true;
            std::raise(SIGINT);
        };
        /* clang-format off */
        detail::menu_widget(temp, ok_callback, &selected);
        if (!success) { return; }
        /* clang-format on */
    }

    const auto& answer = std::get<std::string>(config_data["ANSWER"]);
    auto& partition    = std::get<std::string>(config_data["PARTITION"]);
    /* clang-format off */
    if (answer == SelSwpNone) { return; }
    partition = answer;
    /* clang-format on */

    if (partition == SelSwpFile) {
        const auto& total_memory = utils::exec("grep MemTotal /proc/meminfo | awk \'{print $2/1024}\' | sed \'s/\\..*//\'");
        std::string value{fmt::format("{}M", total_memory)};
        if (!detail::inputbox_widget(value, "\nM = MB, G = GB\n", size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
            return;
        }

        while (utils::exec(fmt::format("echo \"{}\" | grep \"M\\|G\"", value)) == "") {
            detail::msgbox_widget(fmt::format("\n{} Error: M = MB, G = GB\n", SelSwpFile));
            value = fmt::format("{}M", total_memory);
            if (!detail::inputbox_widget(value, "\nM = MB, G = GB\n", size(ftxui::HEIGHT, ftxui::LESS_THAN, 9) | size(ftxui::WIDTH, ftxui::LESS_THAN, 30))) {
                return;
            }
        }

#ifdef NDEVENV
        const auto& swapfile_path = fmt::format("{}/swapfile", mountpoint_info);
        utils::exec(fmt::format("fallocate -l {} {} >/dev/null", value, swapfile_path));
        utils::exec(fmt::format("chmod 600 {}", swapfile_path));
        utils::exec(fmt::format("mkswap {} >/dev/null", swapfile_path));
        utils::exec(fmt::format("swapon {} >/dev/null", swapfile_path));
#endif
        return;
    }

    auto& partitions        = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);
    auto& number_partitions = std::get<std::int32_t>(config_data["NUMBER_PARTITIONS"]);

    // Warn user if creating a new swap
    const auto& swap_part = utils::exec(fmt::format("lsblk -o FSTYPE \"{}\" | grep -i \"swap\"", partition));
    if (swap_part != "swap") {
        const auto& do_swap = detail::yesno_widget(fmt::format("\nmkswap {}\n", partition), size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
        /* clang-format off */
        if (!do_swap) { return; }
        /* clang-format on */

#ifdef NDEVENV
        utils::exec(fmt::format("mkswap {} >/dev/null", partition));
#endif
        spdlog::info("mkswap.{}", partition);
    }

#ifdef NDEVENV
    // Whether existing to newly created, activate swap
    utils::exec(fmt::format("swapon {} >/dev/null", partition));
#endif

    // TODO: reimplement natively
    // Since a partition was used, remove that partition from the list
    const auto& str      = utils::make_multiline(partitions);
    const auto& cmd      = fmt::format("echo \"{0}\" | sed \"s~{1} [0-9]*[G-M]~~\" | sed \"s~{1} [0-9]*\\.[0-9]*[G-M]~~\" | sed \"s~{1}$\' -\'~~\"", str, partition);
    const auto& res_text = utils::exec(cmd);
    partitions           = utils::make_multiline(res_text);
    number_partitions -= 1;
}

void lvm_detect() noexcept {
    const auto& lvm_pv = utils::exec("pvs -o pv_name --noheading 2>/dev/null");
    const auto& lvm_vg = utils::exec("vgs -o vg_name --noheading 2>/dev/null");
    const auto& lvm_lv = utils::exec("lvs -o vg_name,lv_name --noheading --separator - 2>/dev/null");

    if ((lvm_lv != "") && (lvm_vg != "") && (lvm_pv != "")) {
        detail::infobox_widget("\nExisting Logical Volume Management (LVM) detected.\nActivating. Please Wait...\n");
        std::this_thread::sleep_for(std::chrono::seconds(2));
#ifdef NDEVENV
        utils::exec("modprobe dm-mod");
        utils::exec("vgscan >/dev/null 2>&1");
        utils::exec("vgchange -ay >/dev/null 2>&1");
#endif
    }
}

void make_esp() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    const auto& sys_info  = std::get<std::string>(config_data["SYSTEM"]);

    /* clang-format off */
    if (sys_info != "UEFI") { return; }
    /* clang-format on */
    {
        const auto& partitions = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);

        std::int32_t selected{};
        bool success{};
        auto ok_callback = [&] {
            auto src              = partitions[static_cast<std::size_t>(selected)];
            const auto& lines     = utils::make_multiline(src, false, " ");
            config_data["ANSWER"] = lines[0];
            success               = true;
            std::raise(SIGINT);
        };
        /* clang-format off */
        detail::menu_widget(partitions, ok_callback, &selected);
        if (!success) { return; }
        /* clang-format on */
    }

    const auto& answer        = std::get<std::string>(config_data["ANSWER"]);
    const auto& luks          = std::get<std::int32_t>(config_data["LUKS"]);
    auto& partition           = std::get<std::string>(config_data["PARTITION"]);
    partition                 = answer;
    config_data["UEFI_MOUNT"] = "";
    config_data["UEFI_PART"]  = partition;

    // If it is already a fat/vfat partition...
    const auto& ret_status = utils::exec(fmt::format("fsck -N {} | grep fat", partition), true);
    bool do_bootpartition{};
    if (ret_status != "0") {
        const auto& content = fmt::format("\nThe UEFI partition {} has already been formatted.\n\nReformat? Doing so will erase ALL data already on that partition.\n", partition);
        do_bootpartition    = detail::yesno_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
    }
    if (do_bootpartition) {
#ifdef NDEVENV
        utils::exec(fmt::format("mkfs.vfat -F32 {} >/dev/null", partition));
#endif
        spdlog::debug("Formating boot partition with fat/vfat!");
    }

    {
        static constexpr auto MntUefiBody      = "\nSelect UEFI Mountpoint.\n\n/boot/efi is recommended for multiboot systems.\n/boot is required for systemd-boot.\n";
        static constexpr auto MntUefiCrypt     = "\nSelect UEFI Mountpoint.\n\n/boot/efi is recommended for multiboot systems and required for full disk encryption. Encrypted /boot is supported only by grub and can lead to slow startup.\n\n/boot is required for systemd-boot and for refind when using encryption.\n";
        const auto& MntUefiMessage             = (luks == 0) ? MntUefiBody : MntUefiCrypt;
        std::vector<std::string> radiobox_list = {
            "/boot/efi",
            "/boot",
        };
        std::int32_t selected{};
        auto component = Container::Vertical({
            Radiobox(&radiobox_list, &selected),
        });

        auto screen  = ScreenInteractive::Fullscreen();
        auto content = Renderer(component, [&] {
            return component->Render() | center | size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40) | vscroll_indicator;
        });

        config_data["ANSWER"] = "";
        auto ok_callback      = [&] {
            auto src              = radiobox_list[static_cast<std::size_t>(selected)];
            config_data["ANSWER"] = src;
            std::raise(SIGINT);
        };

        ButtonOption button_option{.border = false};
        auto controls_container = detail::controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

        auto controls = Renderer(controls_container, [&] {
            return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
        });

        auto global = Container::Vertical({
            Renderer([&] { return detail::multiline_text(utils::make_multiline(MntUefiMessage)); }),
            Renderer([] { return separator(); }),
            content,
            Renderer([] { return separator(); }),
            controls,
        });

        auto renderer = Renderer(global, [&] {
            return detail::centered_interative_multi("New CLI Installer", global);
        });

        screen.Loop(renderer);
    }

    /* clang-format off */
    if (answer.empty()) { return; }
    /* clang-format on */

    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    auto& uefi_mount            = std::get<std::string>(config_data["UEFI_MOUNT"]);
    uefi_mount                  = answer;
    const auto& path_formated   = fmt::format("{}{}", mountpoint_info, uefi_mount);
#ifdef NDEVENV
    utils::exec(fmt::format("mkdir -p {}", path_formated));
    utils::exec(fmt::format("mount {} {}", partition, path_formated));
#endif
    confirm_mount(path_formated);
}

void mount_partitions() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    // Warn users that they CAN mount partitions without formatting them!
    static constexpr auto content = "\nIMPORTANT: Partitions can be mounted without formatting them\nby selecting the 'Do not format' option listed at the top of\nthe file system menu.\n\nEnsure the correct choices for mounting and formatting\nare made as no warnings will be provided, with the exception of\nthe UEFI boot partition.\n";
    detail::msgbox_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 70));

    // LVM Detection. If detected, activate.
    lvm_detect();

    // Ensure partitions are unmounted (i.e. where mounted previously)
    config_data["INCLUDE_PART"] = "\'part\\|lvm\\|crypt\'";
    utils::umount_partitions();

    // We need to remount the zfs filesystems that have defined mountpoints already
    // zfs mount -aO 2>/dev/null

    // Get list of available partitions
    utils::find_partitions();

    // Add legacy zfs filesystems to the list - these can be mounted but not formatted
    /*for i in $(zfs_list_datasets "legacy"); do
        PARTITIONS="${PARTITIONS} ${i}"
        PARTITIONS="${PARTITIONS} zfs"
        NUMBER_PARTITIONS=$(( NUMBER_PARTITIONS + 1 ))
    done*/

    // Filter out partitions that have already been mounted and partitions that just contain crypt or zfs devices
    auto ignore_part = utils::list_mounted();
    // ignore_part += utils::zfs_list_devs();
    ignore_part += utils::list_containing_crypt();

    // const auto& parts = utils::make_multiline(ignore_part);
    // for (const auto& part : parts) {
    //#ifdef NDEVENV
    // utils::delete_partition_in_list(part);
    //#else
    //        spdlog::debug("{}", part);
    //#endif
    //    }

    // check to see if we already have a zfs root mounted
    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    if (utils::exec(fmt::format("findmnt -ln -o FSTYPE \"{}\"", mountpoint_info)) == "zfs") {
        detail::infobox_widget("\nUsing ZFS root on \'/\'\n");
        std::this_thread::sleep_for(std::chrono::seconds(3));
    } else {
        // Identify and mount root
        {
            std::int32_t selected{};
            bool success{};
            const auto& partitions = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);

            auto ok_callback = [&] {
                auto src                 = partitions[static_cast<std::size_t>(selected)];
                const auto& lines        = utils::make_multiline(src, false, " ");
                config_data["PARTITION"] = lines[0];
                config_data["ROOT_PART"] = lines[0];
                success                  = true;
                std::raise(SIGINT);
            };
            detail::menu_widget(partitions, ok_callback, &selected);
            if (!success)
                return;
        }
        spdlog::info("partition: {}", std::get<std::string>(config_data["PARTITION"]));

        // Reset the mountpoint variable, in case this is the second time through this menu and old state is still around
        config_data["MOUNT"] = "";

        // Format with FS (or skip) -> // Make the directory and mount. Also identify LUKS and/or LVM
        if (!tui::select_filesystem())
            return;
        if (!tui::mount_current_partition())
            return;

        // Extra check if root is on LUKS or lvm
        // get_cryptroot
        // echo "$LUKS_DEV" > /tmp/.luks_dev
        // If the root partition is btrfs, offer to create subvolumus
        if (utils::exec(fmt::format("findmnt -no FSTYPE \"{}\"", mountpoint_info)) == "btrfs") {
            // Check if there are subvolumes already on the btrfs partition
            const auto& subvolumes       = fmt::format("btrfs subvolume list \"{}\"", mountpoint_info);
            const auto& subvolumes_count = utils::exec(fmt::format("{} | wc -l", subvolumes));
            const auto& lines_count      = utils::to_int(subvolumes_count.data());
            if (lines_count > 1) {
                const auto& subvolumes_formated = utils::exec(fmt::format("{} | cut -d\" \" -f9", subvolumes));
                const auto& existing_subvolumes = detail::yesno_widget(fmt::format("\nFound subvolumes {}\n\nWould you like to mount them? \n", subvolumes_formated), size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
                // Pre-existing subvolumes and user wants to mount them
                if (existing_subvolumes) {
                    spdlog::debug("Implement me!");
                }
                // mount_existing_subvols
            } else {
                // No subvolumes present. Make some new ones
                const auto& create_subvolumes = detail::yesno_widget("\nWould you like to create subvolumes in it? \n", size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 75));
                if (create_subvolumes) {
                    spdlog::debug("Implement me!");
                }
                // DIALOG " Your root volume is formatted in btrfs " --yesno "" 0 0 && btrfs_subvolumes
            }
        }
    }

    // We need to remove legacy zfs partitions before make_swap since they can't hold swap
    // local zlegacy
    // for zlegacy in $(zfs_list_datasets "legacy"); do
    //    delete_partition_in_list ${zlegacy}
    // done

    // Identify and create swap, if applicable
    tui::make_swap();

    // Now that swap is done we put the legacy partitions back, unless they are already mounted
    /*for i in $(zfs_list_datasets "legacy"); do
        PARTITIONS="${PARTITIONS} ${i}"
        PARTITIONS="${PARTITIONS} zfs"
        NUMBER_PARTITIONS=$(( NUMBER_PARTITIONS + 1 ))
    done

    for part in $(list_mounted); do
        delete_partition_in_list $part
    done*/

    // All other partitions
    const auto& number_partitions = std::get<std::int32_t>(config_data["NUMBER_PARTITIONS"]);
    const auto& system_info       = std::get<std::string>(config_data["SYSTEM"]);
    const auto& partition         = std::get<std::string>(config_data["PARTITION"]);
    while (number_partitions > 0) {
        {
            std::int32_t selected{};
            bool success{};
            const auto& partitions = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);
            std::vector<std::string> temp{"Done -"};
            temp.reserve(partitions.size());
            ranges::copy(partitions, std::back_inserter(temp));

            auto ok_callback = [&] {
                auto src                 = temp[static_cast<std::size_t>(selected)];
                const auto& lines        = utils::make_multiline(src, false, " ");
                config_data["PARTITION"] = lines[0];
                success                  = true;
                std::raise(SIGINT);
            };
            /* clang-format off */
            detail::menu_widget(temp, ok_callback, &selected);
            if (!success) { return; }
            /* clang-format on */
        }

        if (partition == "Done") {
            make_esp();
            // get_cryptroot();
            // get_cryptboot();
            return;
        }
        config_data["MOUNT"] = "";
        tui::select_filesystem();

        /* clang-format off */
        // Ask user for mountpoint. Don't give /boot as an example for UEFI systems!
        std::string_view mnt_examples = "/boot\n/home\n/var";
        if (system_info == "UEFI") { mnt_examples = "/home\n/var"; }

        std::string value{"/"};
        static constexpr auto ExtPartBody1 = "Specify partition mountpoint. Ensure\nthe name begins with a forward slash (/).\nExamples include:";
        if (!detail::inputbox_widget(value, fmt::format("\n{}\n{}\n", ExtPartBody1, mnt_examples))) { return; }
        /* clang-format on */
        auto& mount_dev = std::get<std::string>(config_data["MOUNT"]);
        mount_dev       = std::move(value);

        // loop while the mountpoint specified is incorrect (is only '/', is blank, or has spaces).
        while ((mount_dev.size() <= 1) || (mount_dev[0] != '/') || std::regex_match(mount_dev, std::regex{"\\s+|\\'"})) {
            // Warn user about naming convention
            detail::msgbox_widget("\nPartition cannot be mounted due to a problem with the mountpoint name.\nA name must be given after a forward slash.\n");
            // Ask user for mountpoint again
            value = "/";
            if (!detail::inputbox_widget(value, fmt::format("\n{}\n{}\n", ExtPartBody1, mnt_examples))) {
                return;
            }
            mount_dev = std::move(value);
        }
        // Create directory and mount.
        tui::mount_current_partition();

        // Determine if a seperate /boot is used.
        // 0 = no seperate boot,
        // 1 = seperate non-lvm boot,
        // 2 = seperate lvm boot. For Grub configuration
        if (mount_dev == "/boot") {
            const auto& cmd             = fmt::format("lsblk -lno TYPE {} | grep \"lvm\"", std::get<std::string>(config_data["PARTITION"]));
            const auto& cmd_out         = utils::exec(cmd);
            config_data["LVM_SEP_BOOT"] = 1;
            if (!cmd_out.empty()) {
                config_data["LVM_SEP_BOOT"] = 2;
            }
        }
    }
}

void create_partitions() noexcept {
    static constexpr std::string_view optwipe = "Securely Wipe Device (optional)";
    static constexpr std::string_view optauto = "Automatic Partitioning";

    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    std::vector<std::string> menu_entries = {
        optwipe.data(),
        optauto.data(),
        "cfdisk",
        "cgdisk",
        "fdisk",
        "gdisk",
        "parted",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        const auto& answer = menu_entries[static_cast<std::size_t>(selected)];
        if (answer != optwipe && answer != optauto) {
            screen.Clear();
            utils::exec(fmt::format("{} {}", answer, std::get<std::string>(config_data["DEVICE"])), true);
            return;
        }

        if (answer == optwipe) {
            utils::secure_wipe();
            return;
        }
        if (answer == optauto) {
            auto_partition();
            return;
        }
    };

    MenuOption menu_option{.on_enter = ok_callback};
    auto menu    = Menu(&menu_entries, &selected, &menu_option);
    auto content = Renderer(menu, [&] {
        return menu->Render() | center | size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    });

    ButtonOption button_option{.border = false};
    auto controls_container = detail::controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    auto global = Container::Vertical({
        content,
        Renderer([] { return separator(); }),
        controls,
    });

    auto renderer = Renderer(global, [&] {
        return detail::centered_interative_multi("New CLI Installer", global);
    });

    screen.Loop(renderer);
}

void install_desktop_system_menu() { }
void install_core_menu() { install_base(); }
void install_custom_menu() { }
void system_rescue_menu() {
    std::vector<std::string> menu_entries = {
        "Install Hardware Drivers",
        "Install Bootloader",
        "Install Packages",
        "Remove Packages",
        "Review Configuration Files",
        "Chroot into Installation",
        "Data Recovery",
        "View System Logs",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 1:
            if (!utils::check_mount() && !utils::check_base()) {
                screen.ExitLoopClosure();
                std::raise(SIGINT);
            }
            tui::install_bootloader();
            break;
        case 2:
            if (!utils::check_mount()) {
                screen.ExitLoopClosure();
                std::raise(SIGINT);
            }
            tui::install_cust_pkgs();
            break;
        case 3:
            if (!utils::check_mount() && !utils::check_base()) {
                screen.ExitLoopClosure();
                std::raise(SIGINT);
            }
            tui::rm_pgs();
            break;
        default:
            screen.ExitLoopClosure();
            std::raise(SIGINT);
            break;
        }
    };

    MenuOption menu_option{.on_enter = ok_callback};
    auto menu    = Menu(&menu_entries, &selected, &menu_option);
    auto content = Renderer(menu, [&] {
        return menu->Render() | center | size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    });

    ButtonOption button_option{.border = false};
    auto controls_container = detail::controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    auto global = Container::Vertical({
        content,
        Renderer([] { return separator(); }),
        controls,
    });

    auto renderer = Renderer(global, [&] {
        return detail::centered_interative_multi("New CLI Installer", global);
    });

    screen.Loop(renderer);
}

void prep_menu() noexcept {
    std::vector<std::string> menu_entries = {
        "Set Virtual Console",
        "List Devices (optional)",
        "Partition Disk",
        "RAID (optional)",
        "Logical Volume Management (optional)",
        "LUKS Encryption (optional)",
        "ZFS (optional)",
        "Mount Partitions",
        "Configure Installer Mirrorlist",
        "Refresh Pacman Keys",
        "Choose pacman cache",
        "Enable fsck hook",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            SPDLOG_ERROR("Implement me!");
            break;
        case 1:
            tui::show_devices();
            break;

        case 2: {
            utils::umount_partitions();
            if (tui::select_device()) {
                tui::create_partitions();
            }
            break;
        }
        case 7:
            tui::mount_partitions();
            break;
        case 3:
        case 4:
        case 5:
        case 6:
        case 8:
        case 9:
        case 10:
            SPDLOG_ERROR("Implement me!");
            break;
        case 11:
            set_fsck_hook();
            break;
        default:
            screen.ExitLoopClosure();
            std::raise(SIGINT);
            break;
        }
    };

    MenuOption menu_option{.on_enter = ok_callback};
    auto menu    = Menu(&menu_entries, &selected, &menu_option);
    auto content = Renderer(menu, [&] {
        return menu->Render() | center | size(HEIGHT, GREATER_THAN, 15) | size(WIDTH, GREATER_THAN, 50);
    });

    ButtonOption button_option{.border = false};
    auto controls_container = detail::controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    auto global = Container::Vertical({
        content,
        Renderer([] { return separator(); }),
        controls,
    });

    auto renderer = Renderer(global, [&] {
        return detail::centered_interative_multi("New CLI Installer", global);
    });

    screen.Loop(renderer);
}

void init() noexcept {
    std::vector<std::string> menu_entries = {
        "Prepare Installation",
        "Install Desktop System",
        "Install CLI System",
        "Install Custom System",
        "System Rescue",
        "Done",
    };

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto ok_callback = [&] {
        switch (selected) {
        case 0:
            tui::prep_menu();
            break;
        case 1: {
            if (!utils::check_mount()) {
                screen.ExitLoopClosure();
                std::raise(SIGINT);
            }
            tui::install_desktop_system_menu();
            break;
        }
        case 2: {
            if (!utils::check_mount()) {
                screen.ExitLoopClosure();
                std::raise(SIGINT);
            }
            tui::install_core_menu();
            break;
        }
        case 3: {
            if (!utils::check_mount()) {
                screen.ExitLoopClosure();
                std::raise(SIGINT);
            }
            tui::install_custom_menu();
            break;
        }
        case 4:
            tui::system_rescue_menu();
            break;
        default:
            screen.ExitLoopClosure();
            std::raise(SIGINT);
            break;
        }
    };

    MenuOption menu_option{.on_enter = ok_callback};
    auto menu    = Menu(&menu_entries, &selected, &menu_option);
    auto content = Renderer(menu, [&] {
        return menu->Render() | center | size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    });

    ButtonOption button_option{.border = false};
    auto controls_container = detail::controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    auto global = Container::Vertical({
        content,
        Renderer([] { return separator(); }),
        controls,
    });

    auto renderer = Renderer(global, [&] {
        return detail::centered_interative_multi("New CLI Installer", global);
    });

    screen.Loop(renderer);
}

}  // namespace tui
