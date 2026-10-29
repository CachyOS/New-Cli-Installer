#include "utils.hpp"
#include "definitions.hpp"
#include "disk.hpp"
#include "global_storage.hpp"
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
#include "gucc/bootloader.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/lvm.hpp"
#include "gucc/partitioning.hpp"
#include "gucc/process.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/system_query.hpp"
#include "gucc/systemd_services.hpp"
#include "gucc/zfs.hpp"

#include <cstdint>  // for int32_t
#include <cstdlib>  // for exit

#include <algorithm>      // for sort, unique, any_of
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

auto encode_ready_partition(const cachyos::installer::PartitionConfig& part) noexcept -> std::expected<std::string, std::string> {
    const auto fields = std::array{part.name, part.mountpoint, part.size, part.fs_name};
    if (std::ranges::any_of(fields, [](std::string_view field) { return field.contains('\t') || field.contains('\n'); })) {
        return std::unexpected(fmt::format(FMT_COMPILE("partition '{}' has a tab or newline in a field"), part.name));
    }
    return fmt::format(FMT_COMPILE("{}\t{}\t{}\t{}\t{}"), part.name, part.mountpoint, part.size, part.fs_name,
        cachyos::installer::partition_type_to_string(part.type));
}

auto decode_ready_partition(std::string_view entry) noexcept -> std::optional<cachyos::installer::PartitionConfig> {
    const auto fields = gucc::utils::split_lines(entry, '\t') | std::ranges::to<std::vector<std::string>>();
    if (fields.size() != 5 || entry.contains('\n')) {
        return std::nullopt;
    }
    return cachyos::installer::partition_type_from_string(fields[4]).transform([&fields](auto type) {
        return cachyos::installer::PartitionConfig{.name = fields[0], .mountpoint = fields[1], .size = fields[2], .fs_name = fields[3], .type = type};
    });
}

auto kernel_from_initramfs_name(std::string_view filename) noexcept -> std::optional<std::string> {
    return gucc::utils::strip_enclosing(filename, "initramfs-"sv, ".img"sv)
        .transform([](std::string_view kernel) { return gucc::utils::strip_suffix(kernel, "-fallback"sv).value_or(kernel); })
        .and_then([](std::string_view kernel) { return kernel.empty() ? std::nullopt : std::optional<std::string>{kernel}; });
}

}  // namespace utils::detail

namespace utils {

auto get_mountpoint() noexcept -> std::string_view {
    return config::get<std::string>("MOUNTPOINT");
}

auto build_install_context() noexcept -> cachyos::installer::InstallContext {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    cachyos::installer::InstallContext ctx{};
    ctx.mountpoint  = utils::get_mountpoint();
    ctx.device      = std::get<std::string>(config_data["DEVICE"]);
    ctx.system_mode = (std::get<std::string>(config_data["SYSTEM"]) == "UEFI"sv)
        ? cachyos::installer::InstallContext::SystemMode::UEFI
        : cachyos::installer::InstallContext::SystemMode::BIOS;
    ctx.hostcache   = std::get<std::int32_t>(config_data["hostcache"]) != 0;

    ctx.partition_schema = std::get<std::vector<gucc::fs::Partition>>(config_data["PARTITION_SCHEMA"]);
    ctx.swap_device      = std::get<std::string>(config_data["SWAP_DEVICE"]);
    ctx.uefi_mount       = std::get<std::string>(config_data["UEFI_MOUNT"]);
    ctx.zfs_zpool_names  = std::get<std::vector<std::string>>(config_data["ZFS_ZPOOL_NAMES"]);

    ctx.crypto.is_luks        = std::get<std::int32_t>(config_data["LUKS"]) != 0;
    ctx.crypto.is_lvm         = std::get<std::int32_t>(config_data["LVM"]) != 0;
    ctx.crypto.luks_dev       = std::get<std::string>(config_data["LUKS_DEV"]);
    ctx.crypto.luks_name      = std::get<std::string>(config_data["LUKS_NAME"]);
    ctx.crypto.luks_uuid      = std::get<std::string>(config_data["LUKS_UUID"]);
    ctx.crypto.luks_root_name = std::get<std::string>(config_data["LUKS_ROOT_NAME"]);
    ctx.crypto.lvm_sep_boot   = std::get<std::int32_t>(config_data["LVM_SEP_BOOT"]);
    ctx.crypto.is_fde         = std::get<std::int32_t>(config_data["fde"]) != 0;

    ctx.kernel          = std::get<std::string>(config_data["KERNEL"]);
    ctx.desktop         = std::get<std::string>(config_data["DE"]);
    ctx.filesystem_name = std::get<std::string>(config_data["FILESYSTEM_NAME"]);
    ctx.keymap          = std::get<std::string>(config_data["KEYMAP"]);
    ctx.server_mode     = std::get<std::int32_t>(config_data["SERVER_MODE"]) != 0;

    ctx.net_profiles_url          = std::get<std::string>(config_data["NET_PROFILES_URL"]);
    ctx.net_profiles_fallback_url = std::get<std::string>(config_data["NET_PROFILES_FALLBACK_URL"]);
    ctx.net_profiles_user_path    = std::get<std::string>(config_data["NET_PROFILES_USER_PATH"]);

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

void arch_chroot(const std::string_view& command) noexcept {
#ifdef NDEVENV
    follow_logged([&] { return cachyos::installer::arch_chroot(command, utils::get_mountpoint()); });
#else
    spdlog::info("[arch_chroot] {}", command);
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
    const auto& zfs_zpool_names = config::get<std::vector<std::string>>("ZFS_ZPOOL_NAMES");
    const auto& swap_device     = config::get<std::string>("SWAP_DEVICE");
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
    const auto& device_info    = config::get<std::string>("DEVICE");
    const auto& system_info    = config::get<std::string>("SYSTEM");
    const auto& bootloader_str = config::get<std::string>("BOOTLOADER");

    const auto& bootloader_opt = gucc::bootloader::bootloader_from_string(bootloader_str);
    if (!bootloader_opt) {
        spdlog::error("Unknown bootloader: {}", bootloader_str);
        return {};
    }

#ifdef NDEVENV
    auto result = cachyos::installer::auto_partition(device_info, system_info, *bootloader_opt);
    if (!result) {
        spdlog::error("{}", result.error());
        return {};
    }
    auto& partitions = *result;
#else
    const auto& boot_mountpoint = utils::bootloader_default_mount(*bootloader_opt, system_info);
    const auto& is_system_efi   = (system_info == "UEFI"sv);
    auto partitions             = gucc::disk::generate_default_partition_schema(device_info, boot_mountpoint, is_system_efi);
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
    const auto& device_info = config::get<std::string>("DEVICE");
#ifdef NDEVENV
    follow_logged([&] { return cachyos::installer::secure_wipe(device_info); });
#else
    spdlog::debug("{}\n", device_info);
#endif
}

// Create user on the system
void create_new_user(const std::string_view& user, const std::string_view& password, const std::string_view& shell) noexcept {
    spdlog::info("default shell: [{}]", shell);

#ifdef NDEVENV
    const cachyos::installer::UserSettings settings{
        .username = std::string{user},
        .password = std::string{password},
        .shell    = std::string{shell},
    };
    const bool hostcache = config::get<std::int32_t>("hostcache") != 0;
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
    const auto number_partitions                        = static_cast<std::int32_t>(partition_list.size());
    config::get<std::vector<std::string>>("PARTITIONS") = std::move(partition_list);
    config::get<std::int32_t>("NUMBER_PARTITIONS")      = number_partitions;

    const auto require = [](std::string_view error_body) {
        tui::detail::msgbox_widget(error_body);
        tui::create_partitions();
    };
    const bool is_uefi = config::get<std::string>("SYSTEM") == "UEFI"sv;
    switch (purpose) {
    case PartitionPurpose::Mount:
        if (number_partitions < (is_uefi ? 2 : 1)) {
            require("\nBIOS systems require a minimum of one partition (ROOT).\n \nUEFI systems require a minimum of two partitions (ROOT and UEFI).\n");
        }
        break;
    case PartitionPurpose::LvmPv:
        if (number_partitions == 0) {
            require("\nThere are no viable partitions available to use Logical Volume Manager.\nA minimum of one is required.\n\nIf LVM is already in use, deactivating it will allow the partition(s)\nused for its Physical Volume(s) to be used again.\n");
        }
        break;
    case PartitionPurpose::Encrypt:
        if (number_partitions < 2) {
            require("\nA minimum of two partitions are required for encryption:\n\n1. Root (/) - standard or lvm partition types.\n\n2. Boot (/boot or /boot/efi) - standard partition types only\n(except lvm where using BIOS Grub).\n");
        }
        break;
    case PartitionPurpose::OpenLuks:
        break;
    }
}

void refresh_partition_list(std::optional<std::function<void()>> on_lvm_active) noexcept {
    utils::lvm_detect(std::move(on_lvm_active));
    utils::umount_partitions();
    if (!gucc::fs::zfs_mount_all()) {
        spdlog::debug("no zfs datasets to remount?");
    }
    utils::find_partitions();
}

void remove_from_partition_list(std::string_view device) noexcept {
    // <device> <size>
    auto& partitions = config::get<std::vector<std::string>>("PARTITIONS");
    std::erase_if(partitions, [device](std::string_view entry) { return gucc::utils::first_field(entry) == device; });
    config::get<std::int32_t>("NUMBER_PARTITIONS") = static_cast<std::int32_t>(partitions.size());
}

void lvm_detect(std::optional<std::function<void()>> func_callback) noexcept {
    const auto& lvm_info = gucc::lvm::detect_lvm();

    if (!lvm_info.is_active()) {
        return;
    }

    if (func_callback.has_value()) {
        func_callback.value()();
    }

#ifdef NDEVENV
    if (!gucc::lvm::activate_lvm()) {
        spdlog::error("Failed to activate LVM");
    }
#endif
}

auto install_from_pkglist(const std::string_view& packages) noexcept -> bool {
    /* clang-format off */
    if (packages.empty()) { return true; }
    /* clang-format on */

#ifdef NDEVENV
    const auto& pkg_vec  = gucc::utils::make_multiline(packages, false, ' ');
    const bool hostcache = config::get<std::int32_t>("hostcache") != 0;
    return follow_logged([&] { return cachyos::installer::install_packages(pkg_vec, utils::get_mountpoint(), hostcache); });
#else
    spdlog::info("Installing from pkglist: '{}'", packages);
    return true;
#endif
}

void install_base(const std::string_view& packages) noexcept {
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

void install_desktop(const std::string_view& desktop) noexcept {
    config::get<std::string>("DE") = std::string{desktop};

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

void remove_pkgs(const std::string_view& packages) noexcept {
    /* clang-format off */
    if (packages.empty()) { return; }
    /* clang-format on */

#ifdef NDEVENV
    const auto& pkg_vec = gucc::utils::make_multiline(packages, false, ' ');
    follow_logged([&] { return cachyos::installer::remove_packages(pkg_vec, utils::get_mountpoint()); });
#endif
}

auto setup_esp_partition(std::string_view device, std::string_view mountpoint, bool format_requested) noexcept -> std::optional<gucc::fs::Partition> {
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

    config::get<std::int32_t>("LUKS")          = 1;
    config::get<std::string>("LUKS_ROOT_NAME") = state.luks_root_name;
    config::get<std::string>("LUKS_DEV")       = state.luks_dev;

    if (!state.luks_uuid.empty()) {
        config::get<std::string>("LUKS_UUID") = state.luks_uuid;
    }
    if (state.is_lvm) {
        config::get<std::int32_t>("LVM") = 1;
    }
    return true;
}

auto recheck_luks() noexcept -> bool {
    const auto result = cachyos::installer::recheck_luks(utils::get_mountpoint(), config::get<std::string>("UEFI_MOUNT"));
    if (!succeeded(result)) {
        return false;
    }
    if (*result) {
        config::get<std::int32_t>("LUKS") = 1;
    }
    return true;
}

// TODO(vnepogodin): refactor further these legacy states/global storage
auto get_cryptboot() noexcept -> bool {
    const auto result = cachyos::installer::detect_crypto_boot(utils::get_mountpoint(), config::get<std::string>("UEFI_MOUNT"));
    if (!succeeded(result)) {
        return false;
    }

    const auto& state = *result;
    if (!state.is_luks) {
        return false;
    }

    config::get<std::int32_t>("LUKS") = 1;
    if (state.is_lvm) {
        config::get<std::int32_t>("LVM") = 1;
    }

    // Merge boot's luks_dev into existing LUKS_DEV (may already have root's entry)
    auto& existing_luks_dev = config::get<std::string>("LUKS_DEV");
    if (!state.luks_uuid.empty() && !existing_luks_dev.contains(state.luks_uuid)) {
        existing_luks_dev = fmt::format(FMT_COMPILE("{} {}"), existing_luks_dev, state.luks_dev);
    }
    return true;
}

// TODO(vnepogodin): refactor further these legacy states/global storage
auto boot_encrypted_setting() noexcept -> bool {
    auto& fde = config::get<std::int32_t>("fde");
    // reinit
    fde = false;

    const auto result = cachyos::installer::boot_encrypted_setting(utils::get_mountpoint(), config::get<std::string>("UEFI_MOUNT"), config::get<std::int32_t>("LUKS") == 1);
    if (!succeeded(result)) {
        return false;
    }
    fde = *result;
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
        tui::detail::msgbox_widget("\nThe CachyOS base must be installed first.\n");
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

    if (result->system_mode == cachyos::installer::InstallContext::SystemMode::UEFI) {
        config::get<std::string>("SYSTEM") = "UEFI";
    }

    // TODO(vnepogodin): Test which nw-client is available
    if (gucc::services::is_active("NetworkManager"sv)) {
        config::get<std::string>("NW_CMD") = "nmtui";
    }
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
        gucc::utils::run_interactive("iwctl");
        break;
    }
}

void enable_autologin([[maybe_unused]] const std::string_view& dm, [[maybe_unused]] const std::string_view& username) noexcept {
#ifdef NDEVENV
    const auto result = cachyos::installer::enable_autologin(dm, username, utils::get_mountpoint());
    if (!result) {
        spdlog::error("Failed to enable autologin: {}", result.error());
    }
#endif
}

bool parse_config(std::string_view config_path) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    spdlog::info("CachyOS installer version '{}'", INSTALLER_VERSION);
    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    spdlog::info("SYSTEM: '{}'", system_info);

    // 1. Open file for reading.
    const auto file_content = gucc::file_utils::read_whole_file(config_path);

    // 2. Parse JSON using installer_config library.
    auto parse_result = cachyos::installer::parse_installer_config(file_content);
    if (!parse_result) {
        if (file_content.empty()) {
            fmt::print(stderr, "Config not found running with defaults\n");
            return true;
        }
        fmt::print(stderr, "Config error: {}\n", parse_result.error());
        return false;
    }

    const auto& config = *parse_result;

    // 3. Validate headless mode requirements.
    if (config.headless_mode) {
        auto validation = cachyos::installer::validate_headless_config(config);
        if (!validation) {
            fmt::print(stderr, FMT_COMPILE("{}\n"), fmt::join(validation.error(), "; "));
            return false;
        }
    }

    // 4. Apply config to global storage.
    config_data["menus"]         = config.menus;
    config_data["HEADLESS_MODE"] = static_cast<std::int32_t>(config.headless_mode);
    config_data["SERVER_MODE"]   = static_cast<std::int32_t>(config.server_mode);

    if (config.headless_mode) {
        spdlog::info("Running in HEADLESS mode!");
    } else {
        spdlog::info("Running in NORMAL mode!");
    }

    if (config.server_mode) {
        spdlog::info("Server profile enabled!");
    }

    if (config.device) {
        config_data["DEVICE"] = *config.device;
    }

    if (config.fs_name) {
        config_data["FILESYSTEM_NAME"] = *config.fs_name;
    }

    // Convert partitions to ready_parts format.
    if (!config.partitions.empty()) {
        std::vector<std::string> ready_partitions{};
        ready_partitions.reserve(config.partitions.size());
        for (const auto& part : config.partitions) {
            auto entry = detail::encode_ready_partition(part);
            if (!entry) {
                fmt::print(stderr, "Config error: {}\n", entry.error());
                return false;
            }
            ready_partitions.emplace_back(std::move(*entry));
        }
        config_data["READY_PARTITIONS"] = std::move(ready_partitions);
    }

    if (config.mount_opts) {
        config_data["MOUNT_OPTS"] = *config.mount_opts;
    }

    // Store subvolume configuration
    config_data["USE_DEFAULT_SUBVOLS"] = static_cast<std::int32_t>(config.use_default_subvolumes);
    if (!config.subvolumes.empty()) {
        std::vector<gucc::fs::BtrfsSubvolume> btrfs_subvols{};
        btrfs_subvols.reserve(config.subvolumes.size());
        for (const auto& sv : config.subvolumes) {
            btrfs_subvols.push_back(gucc::fs::BtrfsSubvolume{.subvolume = sv.subvolume, .mountpoint = sv.mountpoint});
        }
        config_data["BTRFS_SUBVOLUMES"] = std::move(btrfs_subvols);
    }

    if (config.hostname) {
        config_data["HOSTNAME"] = *config.hostname;
    }

    if (config.locale) {
        config_data["LOCALE"] = *config.locale;
    }

    if (config.xkbmap) {
        config_data["XKBMAP"] = *config.xkbmap;
    }

    if (config.timezone) {
        config_data["TIMEZONE"] = *config.timezone;
    }

    if (config.user_name && config.user_pass && config.user_shell) {
        config_data["USER_NAME"]  = *config.user_name;
        config_data["USER_PASS"]  = *config.user_pass;
        config_data["USER_SHELL"] = *config.user_shell;
    }

    if (config.root_pass) {
        config_data["ROOT_PASS"] = *config.root_pass;
    }

    if (config.kernel) {
        config_data["KERNEL"] = *config.kernel;
    }

    if (config.desktop) {
        config_data["DE"] = *config.desktop;
    }

    if (config.bootloader) {
        config_data["BOOTLOADER"] = *config.bootloader;
    }

    if (config.post_install) {
        config_data["POST_INSTALL"] = *config.post_install;
    }

    return true;
}

auto configured_partitions() noexcept -> std::vector<cachyos::installer::PartitionConfig> {
    return config::get<std::vector<std::string>>("READY_PARTITIONS")
        | std::ranges::views::transform(detail::decode_ready_partition)
        | std::ranges::views::join
        | std::ranges::to<std::vector<cachyos::installer::PartitionConfig>>();
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
    utils::arch_chroot("grub-mkconfig -o /boot/grub/grub.cfg");
}

}  // namespace utils
