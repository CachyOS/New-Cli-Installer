#include "tui.hpp"
#include "config.hpp"
#include "definitions.hpp"
#include "utils.hpp"

/* clang-format off */
#include <csignal>                                 // for raise
#include <algorithm>                               // for transform
#include <memory>                                  // for __shared_ptr_access
#include <string>                                  // for basic_string
#include <ftxui/component/captured_mouse.hpp>      // for ftxui
#include <ftxui/component/component.hpp>           // for Renderer, Button
#include <ftxui/component/component_options.hpp>   // for ButtonOption
#include <ftxui/component/screen_interactive.hpp>  // for Component, ScreenI...
#include <ftxui/dom/elements.hpp>                  // for operator|, size
/* clang-format on */

using namespace ftxui;

namespace tui {

ftxui::Element centered_widget(ftxui::Component& container, const std::string_view& title, const ftxui::Element& widget) {
    return vbox({
        //  -------- Title --------------
        text(title.data()) | bold,
        filler(),
        //  -------- Center Menu --------------
        hbox({
            filler(),
            border(vbox({
                widget,
                separator(),
                container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25),
            })),
            filler(),
        }) | center,
        filler(),
    });
}

ftxui::Component controls_widget(const std::array<std::string_view, 2>&& titles, const std::array<std::function<void()>, 2>&& callbacks, ftxui::ButtonOption* button_option) {
    /* clang-format off */
    auto button_ok       = Button(titles[0].data(), callbacks[0], button_option);
    auto button_quit     = Button(titles[1].data(), callbacks[1], button_option);
    /* clang-format on */

    auto container = Container::Horizontal({
        button_ok,
        Renderer([] { return filler() | size(WIDTH, GREATER_THAN, 3); }),
        button_quit,
    });

    return container;
}

ftxui::Element centered_interative_multi(const std::string_view& title, ftxui::Component& widgets) {
    return vbox({
        //  -------- Title --------------
        text(title.data()) | bold,
        filler(),
        //  -------- Center Menu --------------
        hbox({
            filler(),
            border(vbox({
                widgets->Render(),
            })),
            filler(),
        }) | center,
        filler(),
    });
}

ftxui::Element multiline_text(const std::vector<std::string>& lines) {
    Elements multiline;

    std::transform(lines.cbegin(), lines.cend(), std::back_inserter(multiline),
        [=](const std::string& line) -> Element { return text(line); });
    return vbox(std::move(multiline)) | frame;
}

void msgbox_widget(const std::string_view& content, Decorator boxsize = size(HEIGHT, GREATER_THAN, 5)) {
    auto screen = ScreenInteractive::Fullscreen();
    /* clang-format off */
    auto button_option   = ButtonOption();
    button_option.border = false;
    auto button_back     = Button("OK", screen.ExitLoopClosure(), &button_option);
    /* clang-format on */

    auto container = Container::Horizontal({
        button_back,
    });

    std::string tmp{content.data()};
    auto renderer = Renderer(container, [&] {
        return tui::centered_widget(container, "New CLI Installer", multiline_text(utils::make_multiline(tmp)) | hcenter | boxsize);
    });

    screen.Loop(renderer);
}

void menu_widget(const std::vector<std::string>& entries, const std::function<void()>&& ok_callback, std::int32_t* selected) {
    auto screen  = ScreenInteractive::Fullscreen();
    auto menu    = Menu(&entries, selected);
    auto content = Renderer(menu, [&] {
        return menu->Render() | center | size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    });

    ButtonOption button_option{.border = false};
    auto controls_container = controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    auto global = Container::Vertical({
        content,
        Renderer([] { return separator(); }),
        controls,
    });

    auto renderer = Renderer(global, [&] {
        return tui::centered_interative_multi("New CLI Installer", global);
    });

    screen.Loop(renderer);
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
        utils::exec(fmt::format("parted -s {} rm {}", device_info, del_part));
#else
        spdlog::debug("{}", del_part);
#endif
    }

#ifdef NDEVENV
    // Identify the partition table
    const auto& part_table = utils::exec(fmt::format("parted -s {} print | grep -i \'partition table\' | {}", device_info, "awk \'{print $3}\'"));

    // Create partition table if one does not already exist
    if ((system_info == "BIOS") && (part_table != "msdos"))
        utils::exec(fmt::format("parted -s {} mklabel msdos", device_info));
    if ((system_info == "UEFI") && (part_table != "gpt"))
        utils::exec(fmt::format("parted -s {} mklabel gpt", device_info));

    // Create partitions (same basic partitioning scheme for BIOS and UEFI)
    if (system_info == "BIOS")
        utils::exec(fmt::format("parted -s {} mkpart primary ext3 1MiB 513MiB", device_info));
    else
        utils::exec(fmt::format("parted -s {} mkpart ESP fat32 1MiB 513MiB", device_info));

    utils::exec(fmt::format("parted -s {} set 1 boot on", device_info));
    utils::exec(fmt::format("parted -s {} mkpart primary ext3 513MiB 100%", device_info));
#endif

    // Show created partitions
    auto disklist = utils::exec(fmt::format("lsblk {} -o NAME,TYPE,FSTYPE,SIZE", device_info));

    auto screen = ScreenInteractive::Fullscreen();
    /* clang-format off */
    auto button_option   = ButtonOption();
    button_option.border = false;
    auto button_back     = Button("Back", screen.ExitLoopClosure(), &button_option);
    /* clang-format on */

    auto container = Container::Horizontal({
        button_back,
    });

    auto renderer = Renderer(container, [&] {
        return tui::centered_widget(container, "New CLI Installer", multiline_text(utils::make_multiline(disklist)) | size(HEIGHT, GREATER_THAN, 5));
    });

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
    /* clang-format on */

    auto container = Container::Horizontal({
        button_back,
    });

    auto renderer = Renderer(container, [&] {
        return tui::centered_widget(container, "New CLI Installer", multiline_text(utils::make_multiline(lsblk)) | size(HEIGHT, GREATER_THAN, 5));
    });

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
    menu_widget(devices_list, ok_callback, &selected);

    return success;
}

// Set static list of filesystems rather than on-the-fly. Partially as most require additional flags, and
// partially because some don't seem to be viable.
// Set static list of filesystems rather than on-the-fly.
bool select_filesystem() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    // prep variables
    config_data["fs_opts"] = "";
    config_data["CHK_NUM"] = 0;

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
            config_data["CHK_NUM"]    = 16;
            config_data["fs_opts"]    = "autodefrag compress=zlib compress=lzo compress=zstd compress=no compress-force=zlib compress-force=lzo compress-force=zstd discard noacl noatime nodatasum nospace_cache recovery skip_balance space_cache nossd ssd ssd_spread commit=120";
#ifdef NDEVENV
            utils::exec("modprobe btrfs");
#endif
        } else if (file_sys == "ext2") {
            config_data["FILESYSTEM"] = "mkfs.ext2 -q";
        } else if (file_sys == "ext3") {
            config_data["FILESYSTEM"] = "mkfs.ext3 -q";
        } else if (file_sys == "ext4") {
            config_data["FILESYSTEM"] = "mkfs.ext4 -q";
            config_data["CHK_NUM"]    = 8;
            config_data["fs_opts"]    = "data=journal data=writeback dealloc discard noacl noatime nobarrier nodelalloc";
        } else if (file_sys == "f2fs") {
            config_data["FILESYSTEM"] = "mkfs.f2fs -q";
            config_data["fs_opts"]    = "data_flush disable_roll_forward disable_ext_identify discard fastboot flush_merge inline_xattr inline_data inline_dentry no_heap noacl nobarrier noextent_cache noinline_data norecovery";
            config_data["CHK_NUM"]    = 16;
#ifdef NDEVENV
            utils::exec("modprobe f2fs");
#endif
        } else if (file_sys == "jfs") {
            config_data["FILESYSTEM"] = "mkfs.jfs -q";
            config_data["CHK_NUM"]    = 4;
            config_data["fs_opts"]    = "discard errors=continue errors=panic nointegrity";
        } else if (file_sys == "nilfs2") {
            config_data["FILESYSTEM"] = "mkfs.nilfs2 -fq";
            config_data["CHK_NUM"]    = 7;
            config_data["fs_opts"]    = "discard nobarrier errors=continue errors=panic order=relaxed order=strict norecovery";
        } else if (file_sys == "ntfs") {
            config_data["FILESYSTEM"] = "mkfs.ntfs -q";
        } else if (file_sys == "reiserfs") {
            config_data["FILESYSTEM"] = "mkfs.reiserfs -q";
            config_data["CHK_NUM"]    = 5;
            config_data["fs_opts"]    = "acl nolog notail replayonly user_xattr";
        } else if (file_sys == "vfat") {
            config_data["FILESYSTEM"] = "mkfs.vfat -F32";
        } else if (file_sys == "xfs") {
            config_data["FILESYSTEM"] = "mkfs.xfs -f";
            config_data["CHK_NUM"]    = 9;
            config_data["fs_opts"]    = "discard filestreams ikeep largeio noalign nobarrier norecovery noquota wsync";
        }
        success = true;
        std::raise(SIGINT);
    };
    menu_widget(menu_entries, ok_callback, &selected);

    if (!success)
        return false;

    const auto& file_sys  = std::get<std::string>(config_data["FILESYSTEM"]);
    const auto& partition = std::get<std::string>(config_data["PARTITION"]);
    const auto& content   = fmt::format("\nMount {}\n\n! Data on {} will be lost !\n", file_sys, partition);
    msgbox_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 70));

    return success;
}
bool mount_current_partition() noexcept { return true; }

void mount_partitions() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    // Warn users that they CAN mount partitions without formatting them!
    static constexpr std::string_view content = "\nIMPORTANT: Partitions can be mounted without formatting them\nby selecting the 'Do not format' option listed at the top of\nthe file system menu.\n\nEnsure the correct choices for mounting and formatting\nare made as no warnings will be provided, with the exception of\nthe UEFI boot partition.\n";
    msgbox_widget(content, size(HEIGHT, LESS_THAN, 15) | size(WIDTH, LESS_THAN, 70));

    // LVM Detection. If detected, activate.
    // lvm_detect

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

    const auto& parts = utils::make_multiline(ignore_part);
    for (const auto& part : parts) {
#ifdef NDEVENV
        // utils::delete_partition_in_list(part);
#else
        spdlog::debug("{}", part);
#endif
    }

    // check to see if we already have a zfs root mounted
    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    if (utils::exec(fmt::format("findmnt -ln -o FSTYPE \"{}\"", mountpoint_info)) == "zfs") {
        // DIALOG " $_PrepMntPart " --infobox "\n$_zfsFoundRoot\n " 0 0
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
            menu_widget(partitions, ok_callback, &selected);
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

        // delete_partition_in_list "${ROOT_PART}"

        // Extra check if root is on LUKS or lvm
        // get_cryptroot
        // echo "$LUKS_DEV" > /tmp/.luks_dev
        // If the root partition is btrfs, offer to create subvolumus
        if (utils::exec(fmt::format("findmnt -no FSTYPE \"{}\"", mountpoint_info)) == "btrfs") {
            // Check if there are subvolumes already on the btrfs partition
            const auto& subvolumes  = utils::exec(fmt::format("btrfs subvolume list \"{}\" | wc -l", mountpoint_info));
            const auto& lines_count = utils::to_int(subvolumes.data());
            if (lines_count > 1) {
                // DIALOG " The volume has already subvolumes " --yesno "\nFound subvolumes $(btrfs subvolume list ${MOUNTPOUNT} | cut -d" " -f9)\n\nWould you like to mount them? \n " 0 0;
                //  Pre-existing subvolumes and user wants to mount them
                // mount_existing_subvols
            } else {
                // No subvolumes present. Make some new ones
                // DIALOG " Your root volume is formatted in btrfs " --yesno "\nWould you like to create subvolumes in it? \n " 0 0 && btrfs_subvolumes
            }
        }
    }

    // We need to remove legacy zfs partitions before make_swap since they can't hold swap
    // local zlegacy
    // for zlegacy in $(zfs_list_datasets "legacy"); do
    //    delete_partition_in_list ${zlegacy}
    // done

    // Identify and create swap, if applicable
    // tui::make_swap();

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
    /*while [[ $NUMBER_PARTITIONS > 0 ]]; do
        DIALOG " $_PrepMntPart " --menu "\n$_ExtPartBody\n " 0 0 12 "$_Done" $"-" ${PARTITIONS} 2>${ANSWER} || return 0
        PARTITION=$(cat ${ANSWER})

        if [[ $PARTITION == $_Done ]]; then
                make_esp
                get_cryptroot
                get_cryptboot
                echo "$LUKS_DEV" > /tmp/.luks_dev
                return 0;
        else
            MOUNT=""
            select_filesystem

            # Ask user for mountpoint. Don't give /boot as an example for UEFI systems!
            [[ $SYSTEM == "UEFI" ]] && MNT_EXAMPLES="/home\n/var" || MNT_EXAMPLES="/boot\n/home\n/var"
            DIALOG " $_PrepMntPart $PARTITON " --inputbox "\n$_ExtPartBody1$MNT_EXAMPLES\n " 0 0 "/" 2>${ANSWER} || return 0
            MOUNT=$(cat ${ANSWER})

            # loop while the mountpoint specified is incorrect (is only '/', is blank, or has spaces).
            while [[ ${MOUNT:0:1} != "/" ]] || [[ ${#MOUNT} -le 1 ]] || [[ $MOUNT =~ \ |\' ]]; do
                # Warn user about naming convention
                DIALOG " $_ErrTitle " --msgbox "\n$_ExtErrBody\n " 0 0
                # Ask user for mountpoint again
                DIALOG " $_PrepMntPart $PARTITON " --inputbox "\n$_ExtPartBody1$MNT_EXAMPLES\n " 0 0 "/" 2>${ANSWER} || return 0
                MOUNT=$(cat ${ANSWER})
            done

            # Create directory and mount.
            mount_current_partition
            delete_partition_in_list "$PARTITION"

            # Determine if a seperate /boot is used. 0 = no seperate boot, 1 = seperate non-lvm boot,
            # 2 = seperate lvm boot. For Grub configuration
            if  [[ $MOUNT == "/boot" ]]; then
                [[ $(lsblk -lno TYPE ${PARTITION} | grep "lvm") != "" ]] && LVM_SEP_BOOT=2 || LVM_SEP_BOOT=1
            fi
        fi
    done*/
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
    auto menu    = Menu(&menu_entries, &selected);
    auto content = Renderer(menu, [&] {
        return menu->Render() | center | size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    });

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

    ButtonOption button_option{.border = false};
    auto controls_container = controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    auto global = Container::Vertical({
        content,
        Renderer([] { return separator(); }),
        controls,
    });

    auto renderer = Renderer(global, [&] {
        return tui::centered_interative_multi("New CLI Installer", global);
    });

    screen.Loop(renderer);
}

void install_desktop_system_menu() { }
void install_core_menu() { }
void install_custom_menu() { }
void system_rescue_menu() { }

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
    auto menu    = Menu(&menu_entries, &selected);
    auto content = Renderer(menu, [&] {
        return menu->Render() | center | size(HEIGHT, GREATER_THAN, 15) | size(WIDTH, GREATER_THAN, 50);
    });

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
        case 11:
            SPDLOG_ERROR("Implement me!");
            break;
        default:
            screen.ExitLoopClosure();
            std::raise(SIGINT);
            break;
        }
    };

    ButtonOption button_option{.border = false};
    auto controls_container = controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    auto global = Container::Vertical({
        content,
        Renderer([] { return separator(); }),
        controls,
    });

    auto renderer = Renderer(global, [&] {
        return tui::centered_interative_multi("New CLI Installer", global);
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
    auto menu    = Menu(&menu_entries, &selected);
    auto content = Renderer(menu, [&] {
        return menu->Render() | center | size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    });

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
            utils::check_mount();
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

    ButtonOption button_option{.border = false};
    auto controls_container = controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    auto global = Container::Vertical({
        content,
        Renderer([] { return separator(); }),
        controls,
    });

    auto renderer = Renderer(global, [&] {
        return tui::centered_interative_multi("New CLI Installer", global);
    });

    screen.Loop(renderer);
}
}  // namespace tui
