#include "simple_tui.hpp"
#include "disk.hpp"
#include "install_observers.hpp"
#include "installer_data.hpp"
#include "installer_state.hpp"
#include "utils.hpp"
#include "widgets.hpp"

// instlib
#include "cachyos/installer_config.hpp"
#include "cachyos/logging.hpp"
#include "cachyos/orchestrator.hpp"
#include "cachyos/session.hpp"
#include "cachyos/system.hpp"
#include "cachyos/types.hpp"
#include "cachyos/validators.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/bootloader.hpp"
#include "gucc/btrfs.hpp"
#include "gucc/btrfs_query.hpp"
#include "gucc/chwd.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/locale.hpp"
#include "gucc/mount_partitions.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/partitioning.hpp"
#include "gucc/process.hpp"
#include "gucc/ranges_utils.hpp"
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

using cachyos::installer::PartitionConfig;
using cachyos::installer::PartitionType;

// Structure to hold all user selections
struct UserSelections {
    bool server_mode{};

    std::string device;
    std::string filesystem;
    gucc::bootloader::BootloaderType bootloader{};
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
};

template <class Entries>
auto entry_at(const Entries& entries, std::int32_t idx) noexcept -> const auto& {
    return entries.at(static_cast<std::size_t>(idx));
}

// TODO(vnepogodin): that should be common path from instlib/gucc rather than being here
auto make_partitions_prepared(gucc::bootloader::BootloaderType bootloader, std::string_view root_fs, std::string_view mount_opts_info, const std::vector<PartitionConfig>& ready_parts)
    -> std::optional<std::vector<gucc::fs::Partition>> {
    /* clang-format off */
    if (ready_parts.empty()) { spdlog::error("Invalid use! ready parts empty."); return std::nullopt; }
    /* clang-format on */

    const auto mountpoint_info = utils::get_mountpoint();
    // nothing below reformats root/additional partitions
    const auto block_devices = gucc::disk::list_block_devices().value_or(std::vector<gucc::disk::BlockDevice>{});
    const auto find_device   = [&block_devices](std::string_view name) { return gucc::disk::find_device_by_name(block_devices, name); };

    // mount and save the schema
    auto& ctx  = tui::state().inputs.ctx;
    auto& disk = tui::state().disk;

    std::vector<gucc::fs::Partition> partitions{};
    const auto mount_and_add = [&](const PartitionConfig& part, std::string_view fstype) {
        utils::select_filesystem(fstype);
        utils::mount_partition_headless();

        auto part_struct = utils::attach_luks_info(gucc::fs::Partition{
            .fstype     = part.fs_name,
            .mountpoint = part.mountpoint,
            .uuid_str   = find_device(part.name).transform([](const gucc::disk::BlockDevice& dev) { return dev.uuid; }).value_or(std::string{}),
            .device     = part.name,
            .mount_opts = std::string{mount_opts_info},
        });
        utils::dump_partition_to_log(part_struct);
        partitions.emplace_back(std::move(part_struct));
    };

    for (const auto& part : ready_parts) {
        spdlog::debug("\n========\npart name := '{}'\npart mountpoint := '{}'\npart size := '{}'\npart fs := '{}'\npart type := '{}'\n========\n",
            part.name, part.mountpoint, part.size, part.fs_name, cachyos::installer::partition_type_to_string(part.type));

        switch (part.type) {
        case PartitionType::Boot:
            ctx.uefi_mount = part.mountpoint;
            utils::make_esp_headless(partitions, part.name, bootloader, true, part.mountpoint);
            utils::get_cryptroot();
            utils::get_cryptboot();
            spdlog::info("boot partition: name={}", part.name);
            break;
        case PartitionType::Root: {
            disk.partition = part.name;
            disk.mount     = part.mountpoint;
            spdlog::info("root partition: {}", part.name);
            mount_and_add(part, root_fs);

            if (root_fs != "btrfs"sv) {
                break;
            }
            // pre-existing subvolumes are mounted as they are
            const auto& mount_dir = gucc::utils::target_path(mountpoint_info, part.mountpoint);
            if (!gucc::fs::list_btrfs_subvolumes_detailed(mount_dir, ""sv).empty()) {
                if (!utils::mount_existing_subvols(partitions)) {
                    spdlog::error("Failed to mount existing btrfs subvolumes");
                }
                break;
            }
            auto btrfs_subvols = disk.btrfs_subvolumes;
            if (btrfs_subvols.empty() && disk.use_default_subvolumes) {
                btrfs_subvols = utils::default_btrfs_subvolumes();
            }
            if (btrfs_subvols.empty()) {
                break;
            }
#ifdef NDEVENV
            if (!gucc::fs::btrfs_create_subvols(btrfs_subvols, part.name, mountpoint_info, mount_opts_info)) {
                spdlog::error("Failed to create subvolumes automatically");
            }
#endif
            if (!gucc::fs::btrfs_append_subvolumes(partitions, btrfs_subvols)) {
                spdlog::error("Failed to append btrfs subvolumes into partition scheme");
            }
            break;
        }
        case PartitionType::Additional:
            disk.mount     = part.mountpoint;
            disk.partition = part.name;
            spdlog::info("additional partition: {}", part.name);
            mount_and_add(part, part.fs_name);

            // TODO(vnepogodin): these should also be handled by GUCC directly,
            // there is no reason to have the logic in frontend code

            // 0 = no separate boot, 1 = separate non-lvm boot, 2 = separate lvm boot
            if (part.mountpoint == "/boot"sv) {
                const auto boot_dev     = find_device(part.name);
                ctx.crypto.lvm_sep_boot = (boot_dev && boot_dev->type == "lvm"sv) ? 2 : 1;
            }
            break;
        }
    }

    utils::dump_partitions_to_log(partitions);
    spdlog::info("{}\n", gucc::disk::preview_partition_schema(partitions, ctx.device, ctx.is_uefi()));
    return partitions;
}

void apply_user_selections(const UserSelections& selections, const std::vector<PartitionConfig>& ready_parts) noexcept {
    const auto mountpoint = utils::get_mountpoint();
    const auto& st        = tui::state();

    // Prepare disk partitions
    utils::umount_partitions();

    auto partitions = make_partitions_prepared(selections.bootloader, selections.filesystem, st.disk.mount_opts, ready_parts);
    if (!partitions) {
        utils::umount_partitions();
        return;
    }

    const auto sysinfo = cachyos::installer::detect_system();
    if (!sysinfo) {
        spdlog::error("detect_system failed: {}", sysinfo.error());
        utils::umount_partitions();
        return;
    }

    cachyos::installer::InstallContext ctx{};
    ctx.system_mode           = sysinfo->system_mode;
    ctx.mountpoint            = mountpoint;
    ctx.hostcache             = st.inputs.ctx.hostcache;
    ctx.bootloader            = selections.bootloader;
    ctx.kernel                = selections.kernel;
    ctx.desktop               = selections.desktop;
    ctx.filesystem_name       = selections.filesystem;
    ctx.keymap                = selections.xkbmap;
    ctx.server_mode           = selections.server_mode;
    ctx.install_chwd_profiles = !selections.server_mode;

    // TODO(vnepogodin): that should be part of regular lib code rather than specific to simple tui,
    // which should just have the partition schema that it supplies to the lib
    ctx.strategy         = cachyos::installer::partition_strategy::UseExisting{};
    ctx.partition_schema = std::move(*partitions);

    cachyos::installer::SystemSettings sys{};
    sys.hostname = selections.hostname;
    sys.locale   = selections.locale;
    sys.xkbmap   = selections.xkbmap;
    // Most base X11 layouts (us, de, fr, ...) are valid vconsole names too.
    sys.keymap   = selections.xkbmap;
    sys.timezone = selections.timezone;
    sys.hw_clock = cachyos::installer::SystemSettings::HwClock::UTC;

    cachyos::installer::UserSettings user{};
    user.username = selections.user_name;
    user.password = selections.user_pass;
    user.shell    = selections.user_shell;

    cachyos::installer::InstallSession session{gucc::utils::default_runner()};
    tui::detail::ScriptStdoutObserver script_output;
    const auto output_handle = session.add_observer(script_output);

    const auto result = cachyos::installer::run(ctx, sys, user, selections.root_pass, session);
    if (!result.success) {
        for (const auto& err : result.errors) {
            spdlog::error("install: {}", err);
        }
    }
    for (const auto& warn : result.warnings) {
        spdlog::warn("install: {}", warn);
    }

    // Post-install script
    if (!st.post_install.empty()) {
        spdlog::info("Running post-install script...");
        if (!gucc::utils::exec_checked(st.post_install)) {
            spdlog::error("post-install script failed: {}", st.post_install);
        }
    }

    // The orchestrators steps already validate the install,
    // copy the log into the target, and unmount partitions.

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
        fmt::format(FMT_COMPILE("Filesystem opts: {}"), st.disk.mount_opts), 80);

    fmt::print("┌{0:─^{4}}┐\n"
               "│{1: ^{4}}│\n"
               "│{2: ^{4}}│\n"
               "│{3: ^{4}}│\n"
               "└{0:─^{4}}┘\n",
        "",
        fmt::format(FMT_COMPILE("Kernel: {}"), selections.kernel),
        fmt::format(FMT_COMPILE("Desktop: {}"), (!selections.server_mode) ? selections.desktop : "---"),
        fmt::format(FMT_COMPILE("Bootloader: {}"), gucc::bootloader::bootloader_to_string(selections.bootloader)), 80);

    fmt::print("\nInstallation complete!\n");
}

// Step names for the wizard progress indicator
constexpr std::array step_names{
    "Welcome"sv,
    "Device"sv,
    "Bootloader"sv,
    "Filesystem"sv,
    "Hostname"sv,
    "Locale & Keyboard"sv,
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
    // Pre-load data sources
    auto device_list = installer::data::get_device_list();

    const auto bootloaders = cachyos::installer::data::available_bootloaders(tui::state().inputs.ctx.system_mode);
    auto bootloader_list   = bootloaders
        | std::ranges::views::transform([](gucc::bootloader::BootloaderType bl) { return std::string{gucc::bootloader::bootloader_to_string(bl)}; })
        | std::ranges::to<std::vector<std::string>>();

    auto filesystem_list = installer::data::to_vec(installer::data::available_filesystems);

    auto locale_list = gucc::locale::get_possible_locales();
    auto keymap_list = gucc::locale::get_x11_keymap_layouts();
    auto tz_regions  = gucc::timezone::get_timezone_regions();

    auto kernel_list  = installer::data::to_vec(installer::data::available_kernels);
    auto desktop_list = installer::data::to_vec(installer::data::available_desktops);
    auto shell_list   = installer::data::to_vec(installer::data::available_shells);

    const bool server_mode = tui::state().inputs.ctx.server_mode;

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

    // first match preselected, else the first entry
    const auto index_of = [](const std::vector<std::string>& list, auto&& pred) {
        return static_cast<std::int32_t>(gucc::utils::index_if(list, pred).value_or(0));
    };
    sel_locale = index_of(locale_list, [](std::string_view locale) { return locale.contains("en_US.UTF-8"sv); });
    sel_keymap = index_of(keymap_list, [](std::string_view keymap) { return keymap == "us"sv; });

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
            if (auto valid = cachyos::installer::validate_hostname(inp_hostname); !valid) {
                error_msg = valid.error();
                return false;
            }
            break;
        case 7:  // Root Password
            if (inp_root_pass.empty()) {
                error_msg = "Root password cannot be empty!";
                return false;
            }
            if (inp_root_pass != inp_root_pass_confirm) {
                error_msg = "Root passwords do not match!";
                return false;
            }
            break;
        case 8:  // User Account
            if (auto valid = cachyos::installer::validate_username(inp_user_name); !valid) {
                error_msg = valid.error();
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

    // nav calls
    auto go_next = [&] {
        if (!validate_current_step()) {
            return;
        }
        if (current_step < total_steps - 1) {
            ++current_step;
        }
    };
    auto go_back = [&] {
        if (current_step > 0) {
            --current_step;
        }
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

    // Step 5: Locale & Keyboard
    auto locale_menu             = Menu(&locale_list, &sel_locale);
    auto keymap_menu             = Menu(&keymap_list, &sel_keymap, make_menu_option());
    auto locale_keymap_container = Container::Horizontal({locale_menu, keymap_menu});
    auto locale_keymap_renderer  = Renderer(locale_keymap_container, [&] {
        return vbox({
            text("Select your locale and keyboard layout:") | bold,
            separator(),
            hbox({
                vbox({
                    text("Locale:") | bold,
                    locale_menu->Render() | vscroll_indicator | frame | flex,
                }) | flex,
                separator(),
                vbox({
                    text("Keyboard Layout:") | bold,
                    keymap_menu->Render() | vscroll_indicator | frame | flex,
                }) | flex,
            }) | flex,
        });
    });

    // Step 7: Timezone
    auto tz_region_menu = Menu(&tz_regions, &sel_tz_region);
    auto tz_zone_menu   = Menu(&tz_zones, &sel_tz_zone, make_menu_option());
    auto tz_container   = Container::Horizontal({tz_region_menu, tz_zone_menu});
    auto tz_renderer    = Renderer(tz_container, [&] {
        // Update zones when region changes
        if (sel_tz_region != last_tz_region && !tz_regions.empty()) {
            last_tz_region = sel_tz_region;
            tz_zones       = gucc::timezone::get_timezone_zones(entry_at(tz_regions, sel_tz_region));
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
        if (device_list.empty()) {
            return "";
        }
        return installer::data::parse_device_name(entry_at(device_list, sel_device));
    };
    // every mode offers some bootloader
    auto get_selected_bootloader = [&] { return entry_at(bootloaders, sel_bootloader); };
    auto get_selected_filesystem = [&]() -> std::string {
        return entry_at(filesystem_list, sel_filesystem);
    };
    auto get_selected_locale = [&]() -> std::string {
        if (locale_list.empty()) {
            return "";
        }
        return entry_at(locale_list, sel_locale);
    };
    auto get_selected_keymap = [&]() -> std::string {
        if (keymap_list.empty()) {
            return "";
        }
        return entry_at(keymap_list, sel_keymap);
    };
    auto get_selected_timezone = [&]() -> std::string {
        if (tz_regions.empty() || tz_zones.empty()) {
            return "";
        }
        return fmt::format(FMT_COMPILE("{}/{}"), entry_at(tz_regions, sel_tz_region),
            entry_at(tz_zones, sel_tz_zone));
    };
    auto get_selected_kernel = [&]() -> std::string {
        return entry_at(kernel_list, sel_kernel);
    };
    auto get_selected_desktop = [&]() -> std::string {
        if (server_mode) {
            return "---";
        }
        return entry_at(desktop_list, sel_desktop);
    };

    // Step 12: Summary
    auto summary_renderer = Renderer([&] {
        Elements lines;
        lines.push_back(text("Review your selections:") | bold);
        lines.push_back(separator());
        lines.push_back(hbox({text("Device:      ") | bold, text(get_selected_device())}));
        lines.push_back(hbox({text("Bootloader:  ") | bold, text(std::string{gucc::bootloader::bootloader_to_string(get_selected_bootloader())})}));
        lines.push_back(hbox({text("Filesystem:  ") | bold, text(get_selected_filesystem())}));
        lines.push_back(hbox({text("Hostname:    ") | bold, text(inp_hostname)}));
        lines.push_back(hbox({text("Locale:      ") | bold, text(get_selected_locale())}));
        lines.push_back(hbox({text("Keyboard:    ") | bold, text(get_selected_keymap())}));
        lines.push_back(hbox({text("Timezone:    ") | bold, text(get_selected_timezone())}));
        lines.push_back(hbox({text("Root pass:   ") | bold, text("(set)")}));
        lines.push_back(hbox({text("User:        ") | bold, text(inp_user_name)}));
        lines.push_back(hbox({text("User shell:  ") | bold, text(entry_at(shell_list, sel_user_shell))}));
        lines.push_back(hbox({text("Kernel:      ") | bold, text(get_selected_kernel())}));
        lines.push_back(hbox({text("Desktop:     ") | bold, text(get_selected_desktop())}));
        lines.push_back(separator());
        lines.push_back(text("Press Confirm to begin installation.") | center);
        return vbox(std::move(lines));
    });

    // Tab container holding all steps
    auto tab = Container::Tab({
                                  welcome_renderer,        // 0
                                  device_step,             // 1
                                  bootloader_step,         // 2
                                  filesystem_step,         // 3
                                  hostname_step,           // 4
                                  locale_keymap_renderer,  // 5
                                  tz_renderer,             // 6
                                  root_pass_renderer,      // 7
                                  user_renderer,           // 8
                                  kernel_step,             // 9
                                  desktop_step,            // 10
                                  summary_renderer,        // 11
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
        content.push_back(text(fmt::format(FMT_COMPILE("  Step {}/{}:  {}"), current_step + 1, total_steps,
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
    sel.user_shell  = entry_at(shell_list, sel_user_shell);
    sel.kernel      = get_selected_kernel();
    sel.desktop     = (server_mode) ? "" : get_selected_desktop();
    sel.server_mode = server_mode;
    return sel;
}

auto prepare_disk_state(const UserSelections& selections) noexcept -> std::vector<PartitionConfig> {
    auto& st                      = tui::state();
    st.inputs.ctx.device          = selections.device;
    st.inputs.ctx.filesystem_name = selections.filesystem;
    st.bootloader                 = selections.bootloader;

    // configured layout names partitions that already exist
    if (!st.disk.ready_partitions.empty()) {
        utils::refresh_partition_list();
        return st.disk.ready_partitions;
    }

    const auto auto_partitions = utils::auto_partition();
    utils::refresh_partition_list();
    if (auto_partitions.empty()) {
        utils::select_filesystem(selections.filesystem);
        return {};
    }

    st.inputs.ctx.partition_schema = auto_partitions;
    spdlog::info("Auto-partition schema stored with {} entries", auto_partitions.size());

    const auto type_of = [](const gucc::fs::Partition& part) {
        if (part.mountpoint == "/boot"sv || part.mountpoint == "/boot/efi"sv) {
            return PartitionType::Boot;
        }
        return part.is_root() ? PartitionType::Root : PartitionType::Additional;
    };
    const auto to_ready_part = [&type_of](const gucc::fs::Partition& part) {
        return PartitionConfig{.name = part.device, .mountpoint = part.mountpoint, .size = part.size, .fs_name = part.fstype, .type = type_of(part)};
    };
    return auto_partitions | std::ranges::views::transform(to_ready_part) | std::ranges::to<std::vector<PartitionConfig>>();
}

}  // namespace

namespace tui {

void menu_simple() noexcept {
    tui::state().plain_progress = true;

    const auto selections = run_wizard();
    if (!selections) {
        return;
    }

    cachyos::installer::logging::attach_stdout_sink();
    const auto ready_parts = prepare_disk_state(*selections);
    apply_user_selections(*selections, ready_parts);
}

}  // namespace tui
