#include "utils.hpp"
#include "definitions.hpp"
#include "disk.hpp"
#include "installer_state.hpp"
#include "tui.hpp"
#include "widgets.hpp"

// import installer-lib
#include "cachyos/bootloader.hpp"
#include "cachyos/config.hpp"
#include "cachyos/crypto.hpp"
#include "cachyos/disk.hpp"
#include "cachyos/installer_config.hpp"
#include "cachyos/packages.hpp"
#include "cachyos/steps.hpp"
#include "cachyos/system.hpp"
#include "cachyos/validation.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/bootloader.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/lvm.hpp"
#include "gucc/partitioning.hpp"
#include "gucc/process.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/system_query.hpp"
#include "gucc/zfs.hpp"

#include <cstdint>  // for int32_t
#include <cstdlib>  // for exit

#include <algorithm>      // for sort, unique
#include <array>          // for array
#include <chrono>         // for seconds
#include <filesystem>     // for directory_iterator, read_symlink
#include <iostream>       // for basic_istream, cin
#include <ranges>         // for ranges::*
#include <span>           // for span
#include <string>         // for operator==, string, basic_string, allocator
#include <thread>         // for sleep_for
#include <unordered_map>  // for unordered_map
#include <utility>        // for move
#include <vector>         // for vector

#include <fmt/compile.h>
#include <fmt/ranges.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef NDEVENV
#include "follow_process_log.hpp"

#include <cpr/api.h>
#include <cpr/response.h>
#include <cpr/status_codes.h>
#include <cpr/timeout.h>
#endif

/*
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wambiguous-reversed-operator"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#endif

#include <simdjson.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
*/

using namespace std::string_literals;
using namespace std::string_view_literals;
namespace fs = std::filesystem;

namespace {

template <typename T>
auto succeeded(const std::expected<T, std::string>& result) noexcept -> bool {
    if (!result) {
        spdlog::error("{}", result.error());
    }
    return result.has_value();
}

#ifdef NDEVENV
template <std::invocable F>
auto follow_logged(F&& call) noexcept -> bool {
    return tui::detail::follow_task([&call] { return succeeded(std::invoke(call)); });
}
#endif

}  // namespace

namespace utils::detail {

auto kernel_from_initramfs_name(std::string_view filename) noexcept -> std::optional<std::string> {
    return gucc::utils::strip_enclosing(filename, "initramfs-"sv, ".img"sv)
        .transform([](std::string_view kernel) { return gucc::utils::strip_suffix(kernel, "-fallback"sv).value_or(kernel); })
        .and_then([](std::string_view kernel) { return kernel.empty() ? std::nullopt : std::optional<std::string>{kernel}; });
}

}  // namespace utils::detail

namespace utils {

auto get_mountpoint() noexcept -> std::string_view {
    return tui::state().inputs.ctx.mountpoint;
}

auto build_install_context() noexcept -> cachyos::installer::InstallContext {
    const auto& st = tui::state();
    auto ctx       = st.inputs.ctx;
    ctx.bootloader = st.bootloader.value_or(gucc::bootloader::BootloaderType::Grub);
    return ctx;
}

bool is_connected() noexcept {
#ifdef NDEVENV
    return cachyos::installer::is_connected();
#else
    return true;
#endif
}

bool check_root() noexcept {
#ifdef NDEVENV
    return cachyos::installer::check_root();
#else
    return true;
#endif
}

void clear_screen() noexcept {
    static constexpr auto CLEAR_SCREEN_ANSI = "\033[1;1H\033[2J";
    output_inter(FMT_COMPILE("{}"), CLEAR_SCREEN_ANSI);
}

void arch_chroot(const std::vector<std::string>& argv) noexcept {
#ifdef NDEVENV
    follow_logged([&] { return cachyos::installer::arch_chroot(argv, utils::get_mountpoint()); });
#else
    spdlog::info("[arch_chroot] {}", fmt::join(argv, " "));
#endif
}

void dump_partition_to_log(const gucc::fs::Partition& partition) noexcept {
    const std::string_view part_tag = (partition.mountpoint == "/"sv) ? "root partition"sv : "partition"sv;

    spdlog::debug("[DUMP] {}: fs='{}';mountpoint='{}';uuid_str='{}';device='{}';mount_opts='{}';subvolume='{}'",
        part_tag,
        partition.fstype, partition.mountpoint, partition.uuid_str, partition.device, partition.mount_opts, (!partition.subvolume.has_value()) ? "(NONE)"sv : *partition.subvolume);
}

void dump_partitions_to_log(const std::vector<gucc::fs::Partition>& partitions) noexcept {
    for (auto&& partition : partitions) {
        dump_partition_to_log(partition);
    }
}

bool prompt_char(const char* prompt, const char* color, char* read) noexcept {
    fmt::print("{}{}{}\n", color, prompt, RESET);

    std::string tmp{};
    if (!std::getline(std::cin, tmp)) {
        return false;
    }

    const auto& read_char = (tmp.length() == 1) ? tmp[0] : '\0';
    if (read != nullptr) {
        *read = read_char;
    }
    return true;
}

// Unmount partitions.
void umount_partitions() noexcept {
    const auto mountpoint       = utils::get_mountpoint();
    const auto& ctx             = tui::state().inputs.ctx;
    const auto& zfs_zpool_names = ctx.zfs_zpool_names;
    const auto& swap_device     = ctx.swap_device;
#ifdef NDEVENV
    auto result = cachyos::installer::umount_partitions(mountpoint, zfs_zpool_names, swap_device);
    if (!result) {
        spdlog::error("Failed to umount partitions: {}", result.error());
    }
#else
    spdlog::info("Unmounting partitions on {}, zfs zpool names {}, swap_device {}", mountpoint, zfs_zpool_names, swap_device);
#endif
}

// BIOS and UEFI
auto auto_partition() noexcept -> std::vector<gucc::fs::Partition> {
    const auto& st          = tui::state();
    const auto& device_info = st.inputs.ctx.device;
    const auto system_mode  = st.inputs.ctx.system_mode;

    const auto& bootloader_opt = st.bootloader;
    if (!bootloader_opt) {
        spdlog::error("No bootloader selected");
        return {};
    }

#ifdef NDEVENV
    auto result = cachyos::installer::auto_partition(device_info, system_mode, *bootloader_opt);
    if (!result) {
        spdlog::error("{}", result.error());
        return {};
    }
    auto& partitions = *result;
#else
    const auto boot_mountpoint = cachyos::installer::bootloader_default_mount(*bootloader_opt, system_mode);
    const bool is_system_efi   = st.inputs.ctx.is_uefi();
    auto partitions            = gucc::disk::generate_default_partition_schema(device_info, boot_mountpoint, is_system_efi);
    if (partitions.empty()) {
        spdlog::error("Failed to generate default partition schema: it cannot be empty");
        return {};
    }
    spdlog::info("lsblk {} -o NAME,TYPE,FSTYPE,SIZE", device_info);
#endif

    utils::dump_partitions_to_log(partitions);
    return partitions;
}

// Securely destroy all data on a given device.
void secure_wipe() noexcept {
    const auto& device_info = tui::state().inputs.ctx.device;
#ifdef NDEVENV
    follow_logged([&] { return cachyos::installer::secure_wipe(device_info); });
#else
    spdlog::debug("{}\n", device_info);
#endif
}

// Create user on the system
void create_new_user(std::string_view user, std::string_view password, std::string_view shell) noexcept {
    spdlog::info("default shell: [{}]", shell);

#ifdef NDEVENV
    const cachyos::installer::UserSettings settings{
        .username = std::string{user},
        .password = std::string{password},
        .shell    = std::string{shell},
        // empty picks the CachyOS default groups
        .groups = {},
    };
    const bool hostcache = tui::state().inputs.ctx.hostcache;
    follow_logged([&] { return cachyos::installer::create_user(settings, utils::get_mountpoint(), hostcache); });
#else
    spdlog::debug("user := {}, password := {}", user, password);
#endif
}

void find_partitions(PartitionPurpose purpose) noexcept {
    static constexpr std::array kLvmPvTypes{"part"sv, "crypt"sv};
    static constexpr std::array kEncryptTypes{"part"sv, "lvm"sv};
    static constexpr std::array kAnyTypes{"part"sv, "lvm"sv, "crypt"sv};
    const auto include_types = [purpose]() -> std::span<const std::string_view> {
        switch (purpose) {
        case PartitionPurpose::LvmPv:
            return kLvmPvTypes;
        case PartitionPurpose::Encrypt:
            return kEncryptTypes;
        case PartitionPurpose::Mount:
        case PartitionPurpose::OpenLuks:
            break;
        }
        return kAnyTypes;
    }();

    auto partition_list = gucc::disk::list_mountable_partitions(include_types);
    spdlog::info("found partitions:\n{}", fmt::join(partition_list, "\n"));
    const auto number_partitions = partition_list.size();
    tui::state().disk.partitions = std::move(partition_list);

    const auto require = [](std::string_view error_body) {
        tui::detail::msgbox_widget(error_body);
        tui::create_partitions();
    };
    const bool is_uefi = tui::state().inputs.ctx.is_uefi();
    switch (purpose) {
    case PartitionPurpose::Mount:
        if (number_partitions < (is_uefi ? 2U : 1U)) {
            require("BIOS systems require a minimum of one partition (ROOT).\n\nUEFI systems require a minimum of two partitions (ROOT and UEFI).");
        }
        break;
    case PartitionPurpose::LvmPv:
        if (number_partitions == 0) {
            require("There are no viable partitions available to use Logical Volume Manager. A minimum of one is required.\n\nIf LVM is already in use, deactivating it will allow the partition(s) used for its Physical Volume(s) to be used again.");
        }
        break;
    case PartitionPurpose::Encrypt:
        if (number_partitions < 2U) {
            require("A minimum of two partitions are required for encryption:\n\n1. Root (/) - standard or lvm partition types.\n\n2. Boot (/boot or /boot/efi) - standard partition types only (except lvm where using BIOS Grub).");
        }
        break;
    case PartitionPurpose::OpenLuks:
        break;
    }
}

void refresh_partition_list(std::function<void()> on_lvm_active) noexcept {
    utils::lvm_detect(std::move(on_lvm_active));
    utils::umount_partitions();
    if (!gucc::fs::zfs_mount_all()) {
        spdlog::debug("no zfs datasets to remount?");
    }
    utils::find_partitions();
}

void remove_from_partition_list(std::string_view device) noexcept {
    // <device> <size>
    std::erase_if(tui::state().disk.partitions, [device](std::string_view entry) { return gucc::utils::first_field(entry) == device; });
}

void lvm_detect(std::function<void()> on_active) noexcept {
    const auto& lvm_info = gucc::lvm::detect_lvm();

    if (!lvm_info.is_active()) {
        return;
    }

    if (on_active) {
        on_active();
    }

#ifdef NDEVENV
    if (!gucc::lvm::activate_lvm()) {
        spdlog::error("Failed to activate LVM");
    }
#endif
}

auto install_from_pkglist(std::string_view packages) noexcept -> bool {
    /* clang-format off */
    if (packages.empty()) { return true; }
    /* clang-format on */

#ifdef NDEVENV
    const auto& pkg_vec  = gucc::utils::make_multiline(packages, false, ' ');
    const bool hostcache = tui::state().inputs.ctx.hostcache;
    return follow_logged([&] { return cachyos::installer::install_packages(pkg_vec, utils::get_mountpoint(), hostcache); });
#else
    spdlog::info("Installing from pkglist: '{}'", packages);
    return true;
#endif
}

void install_base(std::string_view packages) noexcept {
#ifdef NDEVENV
    // Ensure crypto state is current before building context
    utils::recheck_luks();

    auto ctx   = utils::build_install_context();
    ctx.kernel = std::string{packages};

    const auto task = [&]() -> bool {
        if (!succeeded(cachyos::installer::steps::base(ctx))) {
            return false;
        }
        // NOTE: advanced TUI is now different from common install path, soo needed
        if (auto res = cachyos::installer::steps::enable_services(ctx); !res) {
            spdlog::warn("enable_services: {}", res.error());
        }
        return true;
    };
    if (!tui::detail::follow_task(task)) {
        spdlog::error("Failed to install base");
    }
#else
    spdlog::info("[install_base] packages: {}", packages);
#endif
}

void install_desktop(std::string_view desktop) noexcept {
    tui::state().inputs.ctx.desktop = std::string{desktop};

#ifdef NDEVENV
    // refresh LUKS before context
    utils::recheck_luks();

    const auto desktop_ctx = utils::build_install_context();

    const auto task = [&]() -> bool {
        if (!succeeded(cachyos::installer::steps::desktop(desktop_ctx))
            || !succeeded(cachyos::installer::steps::desktop_configure(desktop_ctx))) {
            return false;
        }
        // NOTE: advanced TUI is now different from common install path, soo needed
        if (auto res = cachyos::installer::steps::enable_services(desktop_ctx); !res) {
            spdlog::warn("enable_services: {}", res.error());
        }
        return true;
    };
    if (!tui::detail::follow_task(task)) {
        spdlog::error("Failed to install desktop");
    }
#else
    spdlog::info("[install_desktop] desktop: {}", desktop);
#endif
}

void remove_pkgs(std::string_view packages) noexcept {
    /* clang-format off */
    if (packages.empty()) { return; }
    /* clang-format on */

#ifdef NDEVENV
    const auto& pkg_vec = gucc::utils::make_multiline(packages, false, ' ');
    follow_logged([&] { return cachyos::installer::remove_packages(pkg_vec, utils::get_mountpoint()); });
#endif
}

auto setup_esp_partition(std::string_view device, std::string_view mountpoint, [[maybe_unused]] bool format_requested) noexcept -> std::optional<gucc::fs::Partition> {
    const auto& mountpoint_info = utils::get_mountpoint();
#ifdef NDEVENV
    auto result = cachyos::installer::setup_esp_partition(device, mountpoint, mountpoint_info, format_requested);
    if (!result) {
        spdlog::error("Failed to setup ESP partition: {}", result.error());
        return std::nullopt;
    }

    auto boot_part_struct = std::move(*result);
#else
    const bool is_boot_ssd = gucc::disk::is_device_ssd(device);
    spdlog::info("[DRY-RUN] Would setup ESP partition {} at {}{}", device, mountpoint_info, mountpoint);
    auto boot_part_struct = gucc::fs::Partition{
        .fstype     = "vfat",
        .mountpoint = std::string{mountpoint},
        .device     = std::string{device},
        .mount_opts = gucc::fs::get_default_mount_opts(gucc::fs::FilesystemType::Vfat, is_boot_ssd),
    };
#endif

    // TODO(vnepogodin): handle luks information
    utils::dump_partition_to_log(boot_part_struct);
    return boot_part_struct;
}

void install_bootloader(gucc::bootloader::BootloaderType bootloader) noexcept {
    /* clang-format off */
    if (!utils::check_base()) { return; }
    /* clang-format on */

#ifdef NDEVENV
    utils::recheck_luks();
    utils::boot_encrypted_setting();

    auto ctx       = utils::build_install_context();
    ctx.bootloader = bootloader;

    if (!follow_logged([&] { return cachyos::installer::steps::bootloader(ctx); })) {
        spdlog::error("Failed to install bootloader");
    }
#else
    spdlog::info("[install_bootloader] {}", gucc::bootloader::bootloader_to_string(bootloader));
#endif
}

auto device_listing(std::string_view columns, std::string_view filter, std::string_view device) noexcept -> std::string {
    // lsblk prints nothing when no device matches
    return gucc::disk::lsblk_table(columns, filter, device)
        .transform([](std::string listing) { return gucc::utils::trim(listing).empty() ? std::string{"No matching block devices."} : std::move(listing); })
        .value_or(std::string{"Failed to list block devices."});
}

std::string list_mounted() noexcept {
    return cachyos::installer::list_mounted_devices(utils::get_mountpoint());
}

std::string list_containing_crypt() noexcept {
    return cachyos::installer::list_containing_crypt();
}

std::string list_non_crypt() noexcept {
    return cachyos::installer::list_non_crypt();
}

auto get_cryptroot() noexcept -> bool {
    const auto result = cachyos::installer::detect_crypto_root(utils::get_mountpoint());
    if (!succeeded(result)) {
        return false;
    }

    const auto& state = *result;
    if (!state.is_luks) {
        return false;
    }

    auto& crypto          = tui::state().inputs.ctx.crypto;
    crypto.is_luks        = true;
    crypto.luks_root_name = state.luks_root_name;
    crypto.luks_dev       = state.luks_dev;

    if (!state.luks_uuid.empty()) {
        crypto.luks_uuid = state.luks_uuid;
    }
    if (state.is_lvm) {
        crypto.is_lvm = true;
    }
    return true;
}

auto recheck_luks() noexcept -> bool {
    auto& ctx         = tui::state().inputs.ctx;
    const auto result = cachyos::installer::recheck_luks(ctx.mountpoint, ctx.uefi_mount);
    if (!succeeded(result)) {
        return false;
    }
    if (*result) {
        ctx.crypto.is_luks = true;
    }
    return true;
}

// TODO(vnepogodin): refactor further these legacy states/global storage
auto get_cryptboot() noexcept -> bool {
    auto& ctx         = tui::state().inputs.ctx;
    const auto result = cachyos::installer::detect_crypto_boot(ctx.mountpoint, ctx.uefi_mount);
    if (!succeeded(result)) {
        return false;
    }

    const auto& state = *result;
    if (!state.is_luks) {
        return false;
    }

    ctx.crypto.is_luks = true;
    if (state.is_lvm) {
        ctx.crypto.is_lvm = true;
    }

    // root's entry may already be there
    auto& existing_luks_dev = ctx.crypto.luks_dev;
    if (!state.luks_uuid.empty() && !existing_luks_dev.contains(state.luks_uuid)) {
        existing_luks_dev = fmt::format(FMT_COMPILE("{} {}"), existing_luks_dev, state.luks_dev);
    }
    return true;
}

// TODO(vnepogodin): refactor further these legacy states/global storage
auto boot_encrypted_setting() noexcept -> bool {
    auto& ctx         = tui::state().inputs.ctx;
    ctx.crypto.is_fde = false;

    const auto result = cachyos::installer::boot_encrypted_setting(ctx.mountpoint, ctx.uefi_mount, ctx.crypto.is_luks);
    if (!succeeded(result)) {
        return false;
    }
    ctx.crypto.is_fde = *result;
    return true;
}

// Ensure that a partition is mounted
bool check_mount() noexcept {
#ifdef NDEVENV
    const auto& mountpoint_info = utils::get_mountpoint();
    return cachyos::installer::check_mount(mountpoint_info);
#else
    return true;
#endif
}

// Ensure that CachyOS has been installed
bool check_base() noexcept {
#ifdef NDEVENV
    const auto& mountpoint_info = utils::get_mountpoint();
    if (!cachyos::installer::check_base_installed(mountpoint_info)) {
        tui::detail::msgbox_widget("The CachyOS base must be installed first.");
        return false;
    }
#endif
    return true;
}

void id_system() noexcept {
    const auto& result = cachyos::installer::detect_system();
    if (!result) {
        spdlog::error("detect_system failed: {}", result.error());
        // TODO(vnepogodin): for interactivity it shouldn't exit out forcefully
        exit(1);
    }

    // default is UEFI, but don't forget BIOS
    tui::state().inputs.ctx.system_mode = result->system_mode;
}

void install_cachyos_repo() noexcept {
#ifdef NDEVENV
    auto result = cachyos::installer::install_cachyos_repo();
    if (!result) {
        spdlog::error("Failed to install cachyos repos: {}", result.error());
    }
#endif
}

bool handle_connection() noexcept {
#ifdef NDEVENV
    using namespace std::chrono_literals;
    bool connected = cachyos::installer::wait_for_connection(15s);

    if (!connected) {
        char type{};
        while (utils::prompt_char("An active network connection could not be detected, do you want to connect using wifi? [y/n]", RED, &type)) {
            if (type != 'n') {
                show_iwctl();
            }
            break;
        }
        connected = utils::is_connected();
    }

    if (connected) {
        utils::install_cachyos_repo();
    }

    return connected;
#else
    utils::install_cachyos_repo();
    return true;
#endif
}

void show_iwctl() noexcept {
    info_inter("\nInstructions to connect to wifi using iwctl:\n");
    info_inter("1 - To find your wifi device name (ex: wlan0) type `device list`\n");
    info_inter("2 - type `station wlan0 scan`, and wait couple seconds\n");
    info_inter("3 - type `station wlan0 get-networks` (find your wifi Network name ex. my_wifi)\n");
    info_inter("4 - type `station wlan0 connect my_wifi` (don't forget to press TAB for auto completion!\n");
    info_inter("5 - type `station wlan0 show` (status should be connected)\n");
    info_inter("6 - type `exit`\n");

    while (utils::prompt_char("Press a key to continue...", CYAN)) {
        tui::detail::run_in_terminal({"/usr/bin/iwctl"s});
        break;
    }
}

void enable_autologin([[maybe_unused]] std::string_view dm, [[maybe_unused]] std::string_view username) noexcept {
#ifdef NDEVENV
    const auto result = cachyos::installer::enable_autologin(dm, username, utils::get_mountpoint());
    if (!result) {
        spdlog::error("Failed to enable autologin: {}", result.error());
    }
#endif
}

auto parse_config(std::string_view config_path) noexcept -> std::expected<cachyos::installer::InstallerConfig, std::string> {
    spdlog::info("CachyOS installer version '{}'", INSTALLER_VERSION);

    const auto file_content = gucc::file_utils::read_whole_file(config_path);
    if (file_content.empty()) {
        spdlog::info("Config '{}' not found or empty, running with defaults", config_path);
        return cachyos::installer::get_default_config();
    }

    auto config = cachyos::installer::parse_installer_config(file_content);
    if (!config) {
        return std::unexpected(std::move(config.error()));
    }
    if (config->headless_mode) {
        if (auto validation = cachyos::installer::validate_headless_config(*config); !validation) {
            return std::unexpected(gucc::utils::join(validation.error(), "; "sv));
        }
    }
    if (config->server_mode) {
        spdlog::info("Server profile enabled!");
    }
    return config;
}

auto setup_luks_keyfile() noexcept -> bool {
#ifdef NDEVENV
    return succeeded(cachyos::installer::setup_luks_keyfile(utils::get_mountpoint()));
#else
    return true;
#endif
}

auto list_installed_kernels(std::string_view boot_dir) noexcept -> std::vector<std::string> {
    std::error_code err{};
    auto kernels = fs::directory_iterator(boot_dir, err)
        | std::ranges::views::transform([](const fs::directory_entry& entry) { return detail::kernel_from_initramfs_name(entry.path().filename().native()); })
        | std::ranges::views::join
        | std::ranges::to<std::vector<std::string>>();

    // filter unique
    std::ranges::sort(kernels);
    const auto [first, last] = std::ranges::unique(kernels);
    kernels.erase(first, last);
    return kernels;
}

auto enabled_display_manager(std::string_view root_mountpoint) noexcept -> std::string {
    const auto& dm_unit = gucc::utils::target_path(root_mountpoint, "/etc/systemd/system/display-manager.service"sv);
    std::error_code err{};
    const auto& target = fs::read_symlink(dm_unit, err);
    if (err) {
        return {};
    }
    return target.stem().native();
}

auto list_home_users(std::string_view root_mountpoint) noexcept -> std::vector<std::string> {
    const auto& home_dir = gucc::utils::target_path(root_mountpoint, "/home"sv);
    std::error_code err{};
    // hidden entries are not users
    return fs::directory_iterator(home_dir, err)
        | std::ranges::views::transform([](const fs::directory_entry& entry) { return entry.path().filename().native(); })
        | std::ranges::views::cache_latest
        | std::ranges::views::filter([](const std::string& name) { return !name.starts_with('.'); })
        | std::ranges::to<std::vector<std::string>>();
}

void grub_mkconfig() noexcept {
#ifdef NDEVENV
    follow_logged([&] { return cachyos::installer::grub_mkconfig(utils::get_mountpoint()); });
#else
    spdlog::info("[grub_mkconfig] {}", utils::get_mountpoint());
#endif
}

}  // namespace utils
