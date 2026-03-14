#include "simple_tui.hpp"
#include "disk.hpp"
#include "global_storage.hpp"
#include "installer_data.hpp"
#include "utils.hpp"
#include "widgets.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/bootloader.hpp"
#include "gucc/btrfs.hpp"
#include "gucc/chwd.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/locale.hpp"
#include "gucc/mount_partitions.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/partitioning.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/system_query.hpp"
#include "gucc/timezone.hpp"
#include "gucc/zfs.hpp"

#include <cstdlib>      // for setenv
#include <sys/mount.h>  // for mount

#include <algorithm>   // for copy
#include <filesystem>  // for exists, is_directory
#include <fstream>     // for ofstream
#include <ranges>      // for ranges::*
#include <string>      // for basic_string
#include <thread>      // for thread

/* clang-format off */
#include <ftxui/component/component.hpp>           // for Renderer, Button
#include <ftxui/component/event.hpp>               // for Event
#include <ftxui/component/screen_interactive.hpp>  // for Component, ScreenI...
#include <ftxui/dom/elements.hpp>                  // for operator|, size
/* clang-format on */

#include <fmt/compile.h>
#include <fmt/ranges.h>

using namespace ftxui;
using namespace std::string_literals;
using namespace std::string_view_literals;

#ifdef NDEVENV
#include "follow_process_log.hpp"
#endif

namespace {

// Structure to hold all user selections
struct UserSelections {
    bool server_mode{};

    std::string device;
    std::string filesystem;
    std::string bootloader;
    std::string hostname;
    std::string locale;
    std::string xkbmap;
    std::string timezone;
    std::string user_name;
    std::string user_pass;
    std::string user_shell;
    std::string root_pass;
    std::string kernel;
    std::string desktop;
    std::string post_install;
    std::string mount_options;
    std::vector<std::string> ready_partitions;
};

void make_esp(std::vector<gucc::fs::Partition>& partitions, std::string_view part_name, gucc::bootloader::BootloaderType bootloader_type,
    bool reformat_part = true, std::string_view boot_part_mountpoint = "(empty)"sv) noexcept {

    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    const auto& sys_info  = std::get<std::string>(config_data["SYSTEM"]);

    /* clang-format off */
    if (sys_info != "UEFI"sv) { return; }
    /* clang-format on */

    auto& partition = std::get<std::string>(config_data["PARTITION"]);
    partition       = std::string{part_name};

    const auto& uefi_mount    = (boot_part_mountpoint == "(empty)"sv)
           ? utils::bootloader_default_mount(bootloader_type, sys_info)
           : boot_part_mountpoint;
    config_data["UEFI_MOUNT"] = std::string{uefi_mount};
    config_data["UEFI_PART"]  = partition;

    // If it is already a fat/vfat partition, skip formatting
    const bool is_fat_part = gucc::utils::exec_checked(fmt::format(FMT_COMPILE("fsck -N {} | grep -q fat"), partition));
    const bool do_format   = !is_fat_part && reformat_part;

    // Apply ESP
    utils::EspSelection esp_selection{
        .device           = partition,
        .mountpoint       = std::string{uefi_mount},
        .format_requested = do_format,
    };
    if (!utils::apply_esp_selection(esp_selection, partitions, true)) {
        spdlog::error("Failed to apply ESP actions");
        return;
    }
}

// Mount a partition without any TUI interaction (force/headless mode)
void mount_current_partition_headless() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& partition  = std::get<std::string>(config_data["PARTITION"]);
    const auto& mount_dev  = std::get<std::string>(config_data["MOUNT"]);
    const auto& fstype     = std::get<std::string>(config_data["FILESYSTEM_NAME"]);

    // Get default mount options based on fs type and device
    const bool is_ssd         = gucc::disk::is_device_ssd(partition);
    const auto& fs_type       = gucc::fs::string_to_filesystem_type(fstype);
    auto mount_opts           = gucc::fs::get_default_mount_opts(fs_type, is_ssd);
    config_data["MOUNT_OPTS"] = mount_opts;

    if (!utils::mount_partition(partition, mountpoint, mount_dev, mount_opts)) {
        spdlog::error("Failed to mount current partition {}", partition);
    }

    // Remove from partition list (same as confirm_mount with force=true)
    auto& partitions        = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);
    auto& number_partitions = std::get<std::int32_t>(config_data["NUMBER_PARTITIONS"]);
    std::erase_if(partitions, [&partition](std::string_view x) { return gucc::utils::contains(x, partition); });
    number_partitions -= 1;
}

auto make_partitions_prepared(std::string_view bootloader_str, std::string_view root_fs, std::string_view mount_opts_info, const auto& ready_parts) -> bool {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    /* clang-format off */
    if (ready_parts.empty()) { spdlog::error("Invalid use! ready parts empty."); return false; }
    /* clang-format on */

    const auto& bootloader_opt = gucc::bootloader::bootloader_from_string(bootloader_str);
    if (!bootloader_opt) {
        spdlog::error("Unknown bootloader: {}", bootloader_str);
        return false;
    }

    const auto& mountpoint_info = utils::get_mountpoint();

    std::vector<gucc::fs::Partition> partitions{};

    std::string root_part{};
    for (auto&& ready_part : ready_parts) {
        auto part_info = gucc::utils::make_multiline(ready_part, false, '\t');

        auto part_name       = part_info[0];
        auto part_mountpoint = part_info[1];
        auto part_size       = part_info[2];
        auto part_fs         = part_info[3];
        auto part_type       = part_info[4];

        spdlog::debug("\n========\npart name := '{}'\npart mountpoint := '{}'\npart size := '{}'\npart fs := '{}'\npart type := '{}'\n========\n",
            part_name, part_mountpoint, part_size, part_fs, part_type);

        if (part_type == "boot"sv) {
            config_data["UEFI_MOUNT"] = part_mountpoint;
            config_data["UEFI_PART"]  = part_name;
            make_esp(partitions, part_name, *bootloader_opt, true, part_mountpoint);
            utils::get_cryptroot();
            utils::get_cryptboot();
            spdlog::info("boot partition: name={}", part_name);
            continue;
        } else if (part_type == "root"sv) {
            config_data["PARTITION"] = part_name;
            config_data["ROOT_PART"] = part_name;
            config_data["MOUNT"]     = part_mountpoint;
            root_part                = part_name;
            spdlog::info("root partition: {}", part_name);

            utils::select_filesystem(root_fs);
            mount_current_partition_headless();

            auto part_struct = gucc::fs::Partition{.fstype = part_fs, .mountpoint = part_mountpoint, .device = root_part, .mount_opts = std::string{mount_opts_info}};

            const auto& part_uuid = gucc::fs::utils::get_device_uuid(part_struct.device);
            part_struct.uuid_str  = part_uuid;

            // get luks information about the current partition
            const auto& luks_name = std::get<std::string>(config_data["LUKS_NAME"]);
            const auto& luks_uuid = std::get<std::string>(config_data["LUKS_UUID"]);
            if (!luks_name.empty()) {
                part_struct.luks_mapper_name = luks_name;
            }
            if (!luks_uuid.empty()) {
                part_struct.luks_uuid = luks_uuid;
            }

            utils::dump_partition_to_log(part_struct);

            partitions.emplace_back(std::move(part_struct));

            // If the root partition is btrfs, offer to create subvolumes
            if (root_fs == "btrfs"sv) {
                // Check if there are subvolumes already on the btrfs partition
                const auto& mount_dir  = fmt::format(FMT_COMPILE("{}{}"), mountpoint_info, part_mountpoint);
                const auto& subvolumes = gucc::utils::exec(fmt::format(FMT_COMPILE("btrfs subvolume list '{}' 2>/dev/null | cut -d' ' -f9"), mount_dir));
                if (!subvolumes.empty()) {
                    // Pre-existing subvolumes and user wants to mount them
                    if (!utils::mount_existing_subvols(partitions)) {
                        spdlog::error("Failed to mount existing btrfs subvolumes");
                    }
                    continue;
                }
                // Get subvolumes from config or use defaults
                const auto& use_default    = std::get<std::int32_t>(config_data["USE_DEFAULT_SUBVOLS"]);
                const auto& config_subvols = std::get<std::vector<gucc::fs::BtrfsSubvolume>>(config_data["BTRFS_SUBVOLUMES"]);
                const auto& btrfs_subvols  = (!config_subvols.empty()) ? config_subvols
                     : (use_default)                                   ? utils::default_btrfs_subvolumes()
                                                                       : std::vector<gucc::fs::BtrfsSubvolume>{};

                if (btrfs_subvols.empty()) {
                    continue;
                }
#ifdef NDEVENV
                gucc::utils::exec(R"(mount | grep 'on /mnt ' | grep -Po '(?<=\().*(?=\))' > /tmp/.root_mount_options)"sv);
                if (!gucc::fs::btrfs_create_subvols(btrfs_subvols, root_part, mountpoint_info, mount_opts_info)) {
                    spdlog::error("Failed to create subvolumes automatically");
                }
#endif
                if (!gucc::fs::btrfs_append_subvolumes(partitions, btrfs_subvols)) {
                    spdlog::error("Failed to append btrfs subvolumes into partition scheme");
                }
            }
            continue;
        } else if (part_type == "additional"sv) {
            config_data["MOUNT"]     = part_mountpoint;
            config_data["PARTITION"] = part_name;
            spdlog::info("additional partition: {}", part_name);

            utils::select_filesystem(part_fs);
            mount_current_partition_headless();

            auto part_struct = gucc::fs::Partition{.fstype = part_fs, .mountpoint = part_mountpoint, .device = part_name, .mount_opts = std::string{mount_opts_info}};

            const auto& part_uuid = gucc::fs::utils::get_device_uuid(part_struct.device);
            part_struct.uuid_str  = part_uuid;

            // get luks information about the current partition
            const auto& luks_name = std::get<std::string>(config_data["LUKS_NAME"]);
            const auto& luks_uuid = std::get<std::string>(config_data["LUKS_UUID"]);
            if (!luks_name.empty()) {
                part_struct.luks_mapper_name = luks_name;
            }
            if (!luks_uuid.empty()) {
                part_struct.luks_uuid = luks_uuid;
            }

            utils::dump_partition_to_log(part_struct);

            partitions.emplace_back(std::move(part_struct));

            // Determine if a separate /boot is used.
            // 0 = no separate boot,
            // 1 = separate non-lvm boot,
            // 2 = separate lvm boot. For Grub configuration
            if (part_mountpoint == "/boot"sv) {
                config_data["LVM_SEP_BOOT"] = 1;

                const auto& boot_devices = gucc::disk::list_block_devices();
                if (boot_devices) {
                    const auto& boot_dev = gucc::disk::find_device_by_name(*boot_devices, part_name);
                    if (boot_dev && boot_dev->type == "lvm"sv) {
                        config_data["LVM_SEP_BOOT"] = 2;
                    }
                }
            }
            continue;
        }
    }

    utils::dump_partitions_to_log(partitions);
    const auto& device_info = std::get<std::string>(config_data["DEVICE"]);
    const auto& sys_info    = std::get<std::string>(config_data["SYSTEM"]);
    spdlog::info("{}\n", gucc::disk::preview_partition_schema(partitions, device_info, sys_info == "UEFI"sv));
    return true;
}

// Load all selections from config (headless mode)
auto load_selections_from_config() noexcept -> UserSelections {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    UserSelections sel{};
    sel.device           = std::get<std::string>(config_data["DEVICE"]);
    sel.filesystem       = std::get<std::string>(config_data["FILESYSTEM_NAME"]);
    sel.bootloader       = std::get<std::string>(config_data["BOOTLOADER"]);
    sel.hostname         = std::get<std::string>(config_data["HOSTNAME"]);
    sel.locale           = std::get<std::string>(config_data["LOCALE"]);
    sel.xkbmap           = std::get<std::string>(config_data["XKBMAP"]);
    sel.timezone         = std::get<std::string>(config_data["TIMEZONE"]);
    sel.user_name        = std::get<std::string>(config_data["USER_NAME"]);
    sel.user_pass        = std::get<std::string>(config_data["USER_PASS"]);
    sel.user_shell       = std::get<std::string>(config_data["USER_SHELL"]);
    sel.root_pass        = std::get<std::string>(config_data["ROOT_PASS"]);
    sel.kernel           = std::get<std::string>(config_data["KERNEL"]);
    sel.desktop          = std::get<std::string>(config_data["DE"]);
    sel.post_install     = std::get<std::string>(config_data["POST_INSTALL"]);
    sel.mount_options    = std::get<std::string>(config_data["MOUNT_OPTS"]);
    sel.ready_partitions = std::get<std::vector<std::string>>(config_data["READY_PARTITIONS"]);
    sel.server_mode      = std::get<std::int32_t>(config_data["SERVER_MODE"]) != 0;

    return sel;
}

void apply_user_selections(const UserSelections& selections) noexcept {
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    // Prepare disk partitions
    config_data["INCLUDE_PART"] = "part\\|lvm\\|crypt";
    utils::umount_partitions();

    if (!make_partitions_prepared(selections.bootloader, selections.filesystem, selections.mount_options, selections.ready_partitions)) {
        utils::umount_partitions();
        return;
    }

    // at this point we should have everything already mounted
    utils::generate_fstab();

    // System configuration
    utils::set_hostname(selections.hostname);
    utils::set_locale(selections.locale);
    utils::set_xkbmap(selections.xkbmap);
    utils::set_timezone(selections.timezone);
    utils::set_hw_clock("utc"sv);

    // User configuration
    utils::set_root_password(selections.root_pass);
    utils::create_new_user(selections.user_name, selections.user_pass, selections.user_shell);

    // Install process
    utils::install_base(selections.kernel);

    if (!selections.server_mode) {
        utils::install_desktop(selections.desktop);
    }

    // Bootloader
    const auto& bootloader_type = gucc::bootloader::bootloader_from_string(selections.bootloader);
    if (bootloader_type) {
        utils::install_bootloader(*bootloader_type);
    } else {
        spdlog::error("Unknown bootloader: {}", selections.bootloader);
    }

    // Hardware drivers
#ifdef NDEVENV
    if (!selections.server_mode) {
        tui::detail::follow_process_log_task_stdout([&](gucc::utils::SubProcess& child) -> bool {
            return gucc::chwd::install_available_profiles(mountpoint, child);
        });
        std::ofstream{fmt::format(FMT_COMPILE("{}/.video_installed"), mountpoint)};
    }
#endif

    // Post-install script
    if (!selections.post_install.empty()) {
        spdlog::info("Running post-install script...");
        auto logger = spdlog::get("cachyos_logger");
        logger->flush();
        gucc::utils::exec(fmt::format(FMT_COMPILE("{} &>>/tmp/cachyos-install.log"), selections.post_install), true);
    }

    // Final cleanup
    utils::final_check();

#ifdef NDEVENV
    // Copy log to installed system
    namespace fs = std::filesystem;
    if (fs::exists("/tmp/cachyos-install.log")) {
        fs::copy_file("/tmp/cachyos-install.log", fmt::format(FMT_COMPILE("{}/cachyos-install.log"), mountpoint), fs::copy_options::overwrite_existing);
    }
    utils::umount_partitions();
#endif

    // Print completion summary
    fmt::print("\n");
    fmt::print("┌{0:─^{5}}┐\n"
               "│{1: ^{5}}│\n"
               "│{2: ^{5}}│\n"
               "│{3: ^{5}}│\n"
               "│{4: ^{5}}│\n"
               "└{0:─^{5}}┘\n",
        "",
        fmt::format(FMT_COMPILE("Mountpoint: {}"), mountpoint),
        fmt::format(FMT_COMPILE("Your device: {}"), selections.device),
        fmt::format(FMT_COMPILE("Filesystem: {}"), selections.filesystem),
        fmt::format(FMT_COMPILE("Filesystem opts: {}"), selections.mount_options), 80);

    fmt::print("┌{0:─^{4}}┐\n"
               "│{1: ^{4}}│\n"
               "│{2: ^{4}}│\n"
               "│{3: ^{4}}│\n"
               "└{0:─^{4}}┘\n",
        "",
        fmt::format(FMT_COMPILE("Kernel: {}"), selections.kernel),
        fmt::format(FMT_COMPILE("Desktop: {}"), (!selections.server_mode) ? selections.desktop : "---"),
        fmt::format(FMT_COMPILE("Bootloader: {}"), selections.bootloader), 80);

    fmt::print("\nInstallation complete!\n");
}

// Step names for the wizard progress indicator
constexpr std::array step_names{
    "Welcome"sv,
    "Device"sv,
    "Bootloader"sv,
    "Filesystem"sv,
    "Hostname"sv,
    "Locale"sv,
    "Keyboard Layout"sv,
    "Timezone"sv,
    "Root Password"sv,
    "User Account"sv,
    "Kernel"sv,
    "Desktop"sv,
    "Summary"sv,
};
constexpr std::int32_t total_steps = static_cast<std::int32_t>(step_names.size());

// Run the interactive wizard and return user selections (or nullopt if cancelled)
auto run_wizard() noexcept -> std::optional<UserSelections> {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    // Pre-load data sources
    auto device_list = installer::data::get_device_list();

    const auto& sys_info      = std::get<std::string>(config_data["SYSTEM"]);
    const auto& available_bls = utils::available_bootloaders(sys_info);
    std::vector<std::string> bootloader_list{};
    bootloader_list.reserve(available_bls.size());
    std::ranges::transform(available_bls, std::back_inserter(bootloader_list),
        [](gucc::bootloader::BootloaderType bl) { return std::string{gucc::bootloader::bootloader_to_string(bl)}; });

    auto filesystem_list = installer::data::to_vec(installer::data::available_filesystems);

    auto locale_list = gucc::locale::get_possible_locales();
    auto keymap_list = gucc::locale::get_x11_keymap_layouts();
    auto tz_regions  = gucc::timezone::get_timezone_regions();

    auto kernel_list  = installer::data::to_vec(installer::data::available_kernels);
    auto desktop_list = installer::data::to_vec(installer::data::available_desktops);
    auto shell_list   = installer::data::to_vec(installer::data::available_shells);

    const auto& server_mode = std::get<std::int32_t>(config_data["SERVER_MODE"]);

    // Wizard state
    std::int32_t current_step{0};
    bool cancelled{false};
    bool confirmed{false};
    std::string error_msg;

    // Selection state for each step
    std::int32_t sel_device{0};
    std::int32_t sel_bootloader{0};
    std::int32_t sel_filesystem{1};
    std::string inp_hostname{"cachyos"};
    std::int32_t sel_locale{0};
    std::int32_t sel_keymap{0};
    std::int32_t sel_tz_region{0};
    std::int32_t sel_tz_zone{0};
    std::string inp_root_pass;
    std::string inp_root_pass_confirm;
    std::string inp_user_name;
    std::string inp_user_pass;
    std::string inp_user_pass_confirm;
    std::int32_t sel_user_shell{0};
    std::int32_t sel_kernel{0};
    std::int32_t sel_desktop{0};

    // Set locale default to en_US.UTF-8 if available
    for (std::size_t i = 0; i < locale_list.size(); ++i) {
        if (locale_list[i].contains("en_US.UTF-8")) {
            sel_locale = static_cast<std::int32_t>(i);
            break;
        }
    }
    // Set keymap default to "us" if available
    for (std::size_t i = 0; i < keymap_list.size(); ++i) {
        if (keymap_list[i] == "us") {
            sel_keymap = static_cast<std::int32_t>(i);
            break;
        }
    }

    // Timezone zone list (depends on selected region)
    std::vector<std::string> tz_zones;
    std::int32_t last_tz_region{-1};

    auto screen = ScreenInteractive::Fullscreen();

    // Validation (defined early so on_enter callbacks can use go_next)
    auto validate_current_step = [&]() -> bool {
        error_msg.clear();
        switch (current_step) {
        case 1:  // Device
            if (device_list.empty()) {
                error_msg = "No devices found!";
                return false;
            }
            break;
        case 4:  // Hostname
            if (inp_hostname.empty()) {
                error_msg = "Hostname cannot be empty!";
                return false;
            }
            break;
        case 8:  // Root Password
            if (inp_root_pass.empty()) {
                error_msg = "Root password cannot be empty!";
                return false;
            }
            if (inp_root_pass != inp_root_pass_confirm) {
                error_msg = "Root passwords do not match!";
                return false;
            }
            break;
        case 9:  // User Account
            if (inp_user_name.empty()) {
                error_msg = "Username cannot be empty!";
                return false;
            }
            if (inp_user_pass.empty()) {
                error_msg = "User password cannot be empty!";
                return false;
            }
            if (inp_user_pass != inp_user_pass_confirm) {
                error_msg = "User passwords do not match!";
                return false;
            }
            break;
        default:
            break;
        }
        return true;
    };

    // Navigation callbacks (defined early so menu on_enter can use go_next)
    auto go_next = [&] {
        if (!validate_current_step())
            return;
        if (current_step < total_steps - 1)
            ++current_step;
    };
    auto go_back = [&] {
        if (current_step > 0)
            --current_step;
    };
    auto go_cancel = [&] {
        cancelled = true;
        screen.ExitLoopClosure()();
    };
    auto go_confirm = [&] {
        if (!validate_current_step())
            return;
        confirmed = true;
        screen.ExitLoopClosure()();
    };

    // Menu option with Enter advancing to next step
    auto menu_on_enter    = [&] { go_next(); };
    auto make_menu_option = [&] {
        auto opt     = MenuOption::Vertical();
        opt.on_enter = menu_on_enter;
        return opt;
    };
    // Single-line input option (no newlines, Enter advances to next step)
    auto make_input_option = [&](bool is_password = false) {
        InputOption opt;
        opt.password  = is_password;
        opt.multiline = false;
        opt.on_enter  = menu_on_enter;
        return opt;
    };

    // Build step components
    // Step 0: Welcome
    auto welcome_renderer = Renderer([&] {
        return vbox({
            text("Welcome to the CachyOS Installer") | bold | center,
            separator(),
            text("This wizard will guide you through the installation process.") | center,
            text("Use Back/Next to navigate between steps.") | center,
            text("All changes are applied only after you confirm on the Summary step.") | center,
        });
    });

    // Step 1: Device
    auto device_step = tui::detail::wizard_menu_step("Select the target installation device:", device_list, &sel_device, menu_on_enter);

    // Step 2: Bootloader
    auto bootloader_step = tui::detail::wizard_menu_step("Select your bootloader:", bootloader_list, &sel_bootloader, menu_on_enter);

    // Step 3: Filesystem
    auto filesystem_step = tui::detail::wizard_menu_step("Select your filesystem:", filesystem_list, &sel_filesystem, menu_on_enter);

    // Step 4: Hostname
    auto hostname_step = tui::detail::wizard_input_step("Enter your hostname:", &inp_hostname, "hostname", false, menu_on_enter);

    // Step 5: Locale
    auto locale_step = tui::detail::wizard_menu_step("Select your locale:", locale_list, &sel_locale, menu_on_enter);

    // Step 6: Keyboard Layout
    auto keymap_step = tui::detail::wizard_menu_step("Select your keyboard layout:", keymap_list, &sel_keymap, menu_on_enter);

    // Step 7: Timezone
    auto tz_region_menu = Menu(&tz_regions, &sel_tz_region);
    auto tz_zone_menu   = Menu(&tz_zones, &sel_tz_zone, make_menu_option());
    auto tz_container   = Container::Horizontal({tz_region_menu, tz_zone_menu});
    auto tz_renderer    = Renderer(tz_container, [&] {
        // Update zones when region changes
        if (sel_tz_region != last_tz_region && !tz_regions.empty()) {
            last_tz_region = sel_tz_region;
            tz_zones       = gucc::timezone::get_timezone_zones(tz_regions[static_cast<std::size_t>(sel_tz_region)]);
            sel_tz_zone    = 0;
        }
        return vbox({
            text("Select your timezone:") | bold,
            separator(),
            hbox({
                vbox({
                    text("Region:") | bold,
                    tz_region_menu->Render() | vscroll_indicator | frame | flex,
                }) | flex,
                separator(),
                vbox({
                    text("City:") | bold,
                    tz_zone_menu->Render() | vscroll_indicator | frame | flex,
                }) | flex,
            }) | flex,
        });
    });

    // Step 8: Root Password
    auto root_pass_input         = Input(&inp_root_pass, "password", make_input_option(true));
    auto root_pass_confirm_input = Input(&inp_root_pass_confirm, "confirm password", make_input_option(true));
    auto root_pass_container     = Container::Vertical({root_pass_input, root_pass_confirm_input});
    auto root_pass_renderer      = Renderer(root_pass_container, [&] {
        return vbox({
            text("Set root password:") | bold,
            separator(),
            text("Password:"),
            root_pass_input->Render(),
            text("Confirm password:"),
            root_pass_confirm_input->Render(),
        });
    });

    // Step 9: User Account
    auto user_name_input         = Input(&inp_user_name, "username", make_input_option());
    auto user_pass_input         = Input(&inp_user_pass, "password", make_input_option(true));
    auto user_pass_confirm_input = Input(&inp_user_pass_confirm, "confirm password", make_input_option(true));
    auto user_shell_menu         = Menu(&shell_list, &sel_user_shell, make_menu_option());
    auto user_container          = Container::Vertical({user_name_input, user_pass_input, user_pass_confirm_input, user_shell_menu});
    auto user_renderer           = Renderer(user_container, [&] {
        return vbox({
            text("Create user account:") | bold,
            separator(),
            text("Username:"),
            user_name_input->Render(),
            text("Password:"),
            user_pass_input->Render(),
            text("Confirm password:"),
            user_pass_confirm_input->Render(),
            separator(),
            text("Default shell:"),
            user_shell_menu->Render() | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, 5),
        });
    });

    // Step 10: Kernel
    auto kernel_step = tui::detail::wizard_menu_step("Select your kernel:", kernel_list, &sel_kernel, menu_on_enter);

    // Step 11: Desktop
    auto desktop_inner = tui::detail::wizard_menu_step("Select your desktop environment:", desktop_list, &sel_desktop, menu_on_enter);
    auto desktop_step  = Renderer(desktop_inner, [&] {
        if (server_mode) {
            return vbox({
                text("Desktop Environment") | bold,
                separator(),
                text("Server mode is enabled. No desktop will be installed.") | center,
            });
        }
        return desktop_inner->Render();
    });

    // Helper to get current selection strings
    auto get_selected_device = [&]() -> std::string {
        if (device_list.empty())
            return "";
        return installer::data::parse_device_name(device_list[static_cast<std::size_t>(sel_device)]);
    };
    auto get_selected_bootloader = [&]() -> std::string {
        if (bootloader_list.empty())
            return "";
        return bootloader_list[static_cast<std::size_t>(sel_bootloader)];
    };
    auto get_selected_filesystem = [&]() -> std::string {
        return filesystem_list[static_cast<std::size_t>(sel_filesystem)];
    };
    auto get_selected_locale = [&]() -> std::string {
        if (locale_list.empty())
            return "";
        return locale_list[static_cast<std::size_t>(sel_locale)];
    };
    auto get_selected_keymap = [&]() -> std::string {
        if (keymap_list.empty())
            return "";
        return keymap_list[static_cast<std::size_t>(sel_keymap)];
    };
    auto get_selected_timezone = [&]() -> std::string {
        if (tz_regions.empty() || tz_zones.empty())
            return "";
        return fmt::format("{}/{}", tz_regions[static_cast<std::size_t>(sel_tz_region)],
            tz_zones[static_cast<std::size_t>(sel_tz_zone)]);
    };
    auto get_selected_kernel = [&]() -> std::string {
        return kernel_list[static_cast<std::size_t>(sel_kernel)];
    };
    auto get_selected_desktop = [&]() -> std::string {
        if (server_mode)
            return "---";
        return desktop_list[static_cast<std::size_t>(sel_desktop)];
    };

    // Step 12: Summary
    auto summary_renderer = Renderer([&] {
        Elements lines;
        lines.push_back(text("Review your selections:") | bold);
        lines.push_back(separator());
        lines.push_back(hbox({text("Device:      ") | bold, text(get_selected_device())}));
        lines.push_back(hbox({text("Bootloader:  ") | bold, text(get_selected_bootloader())}));
        lines.push_back(hbox({text("Filesystem:  ") | bold, text(get_selected_filesystem())}));
        lines.push_back(hbox({text("Hostname:    ") | bold, text(inp_hostname)}));
        lines.push_back(hbox({text("Locale:      ") | bold, text(get_selected_locale())}));
        lines.push_back(hbox({text("Keyboard:    ") | bold, text(get_selected_keymap())}));
        lines.push_back(hbox({text("Timezone:    ") | bold, text(get_selected_timezone())}));
        lines.push_back(hbox({text("Root pass:   ") | bold, text("(set)")}));
        lines.push_back(hbox({text("User:        ") | bold, text(inp_user_name)}));
        lines.push_back(hbox({text("User shell:  ") | bold, text(shell_list[static_cast<std::size_t>(sel_user_shell)])}));
        lines.push_back(hbox({text("Kernel:      ") | bold, text(get_selected_kernel())}));
        lines.push_back(hbox({text("Desktop:     ") | bold, text(get_selected_desktop())}));
        lines.push_back(separator());
        lines.push_back(text("Press Confirm to begin installation.") | center);
        return vbox(std::move(lines));
    });

    // Tab container holding all steps
    auto tab = Container::Tab({
                                  welcome_renderer,    // 0
                                  device_step,         // 1
                                  bootloader_step,     // 2
                                  filesystem_step,     // 3
                                  hostname_step,       // 4
                                  locale_step,         // 5
                                  keymap_step,         // 6
                                  tz_renderer,         // 7
                                  root_pass_renderer,  // 8
                                  user_renderer,       // 9
                                  kernel_step,         // 10
                                  desktop_step,        // 11
                                  summary_renderer,    // 12
                              },
        &current_step);

    // Navigation buttons
    auto back_btn    = Button(" Back ", go_back);
    auto next_btn    = Button(" Next ", go_next);
    auto confirm_btn = Button(" Confirm ", go_confirm);
    auto cancel_btn  = Button(" Cancel ", go_cancel);

    // Bottom navigation container (all buttons, visibility controlled in renderer)
    auto nav_container = Container::Horizontal({back_btn, next_btn, confirm_btn, cancel_btn});

    // Main layout
    auto main_container = Container::Vertical({tab, nav_container});

    // Intercept Tab/Shift+Tab to toggle focus between step content and nav buttons
    // without changing Menu selections
    auto main_with_tab = CatchEvent(main_container, [&](Event event) -> bool {
        if (event == Event::Tab) {
            // Move focus from step content to nav buttons (or cycle within nav)
            if (main_container->ActiveChild() == tab) {
                main_container->SetActiveChild(nav_container);
            } else {
                main_container->SetActiveChild(tab);
            }
            return true;
        }
        if (event == Event::TabReverse) {
            if (main_container->ActiveChild() == nav_container) {
                main_container->SetActiveChild(tab);
            } else {
                main_container->SetActiveChild(nav_container);
            }
            return true;
        }
        return false;
    });

    auto main_renderer = Renderer(main_with_tab, [&] {
        const bool is_first   = (current_step == 0);
        const bool is_summary = (current_step == total_steps - 1);

        // Navigation bar
        Elements nav_elements;
        if (!is_first) {
            nav_elements.push_back(back_btn->Render());
        }
        nav_elements.push_back(filler());
        if (is_summary) {
            nav_elements.push_back(confirm_btn->Render());
        } else {
            nav_elements.push_back(next_btn->Render());
        }
        nav_elements.push_back(filler());
        nav_elements.push_back(cancel_btn->Render());

        Elements content;
        content.push_back(text("  CachyOS Installer  ") | bold | center);
        content.push_back(separator());
        content.push_back(text(fmt::format("  Step {}/{}:  {}", current_step + 1, total_steps,
                              step_names[static_cast<std::size_t>(current_step)]))
            | bold);
        content.push_back(separator());
        content.push_back(tab->Render() | flex);

        // Error message
        if (!error_msg.empty()) {
            content.push_back(separator());
            content.push_back(text(error_msg) | color(Color::Red) | bold | center);
        }

        content.push_back(separator());
        content.push_back(hbox(std::move(nav_elements)));

        return vbox(std::move(content)) | border;
    });

    screen.Loop(main_renderer);

    if (cancelled || !confirmed) {
        return std::nullopt;
    }

    // Build UserSelections from wizard state
    UserSelections sel{};
    sel.device      = get_selected_device();
    sel.bootloader  = get_selected_bootloader();
    sel.filesystem  = get_selected_filesystem();
    sel.hostname    = inp_hostname;
    sel.locale      = get_selected_locale();
    sel.xkbmap      = get_selected_keymap();
    sel.timezone    = get_selected_timezone();
    sel.root_pass   = inp_root_pass;
    sel.user_name   = inp_user_name;
    sel.user_pass   = inp_user_pass;
    sel.user_shell  = shell_list[static_cast<std::size_t>(sel_user_shell)];
    sel.kernel      = get_selected_kernel();
    sel.desktop     = (server_mode) ? "" : get_selected_desktop();
    sel.server_mode = server_mode != 0;

    // Transfer config values that were loaded from settings.json
    sel.post_install     = std::get<std::string>(config_data["POST_INSTALL"]);
    sel.mount_options    = std::get<std::string>(config_data["MOUNT_OPTS"]);
    sel.ready_partitions = std::get<std::vector<std::string>>(config_data["READY_PARTITIONS"]);

    return sel;
}

// Prepare disk state before applying selections (common for wizard and headless)
void prepare_disk_state(const UserSelections& selections) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    // Set device
    config_data["DEVICE"]          = selections.device;
    config_data["BOOTLOADER"]      = selections.bootloader;
    config_data["FILESYSTEM_NAME"] = selections.filesystem;

    utils::auto_partition();
    utils::lvm_detect();

    config_data["INCLUDE_PART"] = "part\\|lvm\\|crypt";
    utils::umount_partitions();

    gucc::utils::exec("zfs mount -aO &>/dev/null");
    utils::find_partitions();

    auto ignore_part = utils::list_mounted();
    ignore_part += gucc::fs::zfs_list_devs();
    ignore_part += utils::list_containing_crypt();

    auto& fs_name = std::get<std::string>(config_data["FILESYSTEM_NAME"]);
    if (fs_name.empty()) {
        fs_name = selections.filesystem;
    }

    auto& ready_parts = std::get<std::vector<std::string>>(config_data["READY_PARTITIONS"]);
    if (ready_parts.empty()) {
        utils::select_filesystem(selections.filesystem);
    }

    if (ready_parts.empty()) {
        spdlog::info("TODO: auto make layout(ready parts)!");
    }
}

}  // namespace

namespace tui {

void menu_simple() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    const auto& headless  = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);

    // Set simple mode flag for utils functions
    config_data["SIMPLE_MODE"] = 1;

    if (headless) {
        // All values pre-filled from settings.json, skip wizard
        auto selections = load_selections_from_config();
        prepare_disk_state(selections);
        apply_user_selections(selections);
        return;
    }

    // Run wizard
    auto selections = run_wizard();
    if (!selections)
        return;  // cancelled

    // Apply selections
    prepare_disk_state(*selections);
    apply_user_selections(*selections);
}

}  // namespace tui
