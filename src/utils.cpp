#include "utils.hpp"
#include "definitions.hpp"
#include "disk.hpp"
#include "global_storage.hpp"
#include "installer_config.hpp"
#include "tui.hpp"
#include "widgets.hpp"

// import installer-lib
#include "cachyos/bootloader.hpp"
#include "cachyos/config.hpp"
#include "cachyos/crypto.hpp"
#include "cachyos/disk.hpp"
#include "cachyos/packages.hpp"
#include "cachyos/system.hpp"
#include "cachyos/validation.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/bootloader.hpp"
#include "gucc/crypto_detection.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/lvm.hpp"
#include "gucc/partitioning.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/system_query.hpp"

#include <cstdint>  // for int32_t
#include <cstdlib>  // for exit

#include <chrono>         // for seconds
#include <iostream>       // for basic_istream, cin
#include <string>         // for operator==, string, basic_string, allocator
#include <thread>         // for sleep_for
#include <unordered_map>  // for unordered_map

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

#include "gucc/subprocess.hpp"

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

namespace utils {

auto get_mountpoint() noexcept -> std::string_view {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    return std::get<std::string>(config_data["MOUNTPOINT"]);
}

auto build_install_context() noexcept -> cachyos::installer::InstallContext {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    cachyos::installer::InstallContext ctx{};
    ctx.mountpoint  = std::get<std::string>(config_data["MOUNTPOINT"]);
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

void arch_chroot(const std::string_view& command, bool follow) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

#ifdef NDEVENV
    const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
    if (!follow) {
        if (!gucc::utils::arch_chroot_checked(command, mountpoint)) {
            spdlog::error("Failed to run in arch-chroot: {}", command);
        }
        return;
    }

    const auto& simple_mode = std::get<std::int32_t>(config_data["SIMPLE_MODE"]);
    const auto task         = [&](gucc::utils::SubProcess& child) -> bool {
        auto result = cachyos::installer::arch_chroot(command, mountpoint, child);
        if (!result) {
            spdlog::error("{}", result.error());
            return false;
        }
        return true;
    };
    if (simple_mode || headless_mode) {
        tui::detail::follow_process_log_task_stdout(task);
    } else {
        tui::detail::follow_process_log_task(task);
    }
#else
    spdlog::info("[arch_chroot] {}", command);
#endif
}

void dump_to_log(const std::string& data) noexcept {
    spdlog::info("[DUMP_TO_LOG] :=\n{}", data);
}

void dump_settings_to_log() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    std::string out{};
    for (const auto& [key, value] : config_data) {
        const auto& value_formatted = std::visit([](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::vector<gucc::fs::Partition>>) {
                return fmt::format("<partition_schema: {} entries>", arg.size());
            } else if constexpr (std::is_same_v<T, std::vector<gucc::fs::BtrfsSubvolume>>) {
                return fmt::format("<btrfs_subvolumes: {} entries>", arg.size());
            } else {
                return fmt::format("{}", arg);
            }
        },
            value);
        out += fmt::format("Option: [{}], Value: [{}]\n", key, value_formatted);
    }
    spdlog::info("Settings:\n{}", out);
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

// install a pkg in the live session if not installed
void inst_needed(const std::string_view& pkg) noexcept {
#ifdef NDEVENV
    auto* config_instance     = Config::instance();
    auto& config_data         = config_instance->data();
    const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
    const auto& simple_mode   = std::get<std::int32_t>(config_data["SIMPLE_MODE"]);

    const auto task = [&](gucc::utils::SubProcess& child) -> bool {
        auto result = cachyos::installer::install_needed(pkg, child);
        if (!result) {
            spdlog::error("{}", result.error());
            return false;
        }
        return true;
    };
    if (simple_mode || headless_mode) {
        tui::detail::follow_process_log_task_stdout(task);
    } else {
        tui::detail::follow_process_log_task(task);
    }
#else
    spdlog::info("Installing needed: '{}'", pkg);
#endif
}

// Unmount partitions.
void umount_partitions() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& mountpoint      = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& zfs_zpool_names = std::get<std::vector<std::string>>(config_data["ZFS_ZPOOL_NAMES"]);
    const auto& swap_device     = std::get<std::string>(config_data["SWAP_DEVICE"]);
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
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& device_info    = std::get<std::string>(config_data["DEVICE"]);
    const auto& system_info    = std::get<std::string>(config_data["SYSTEM"]);
    const auto& bootloader_str = std::get<std::string>(config_data["BOOTLOADER"]);

    const auto& bootloader_opt = gucc::bootloader::bootloader_from_string(bootloader_str);
    if (!bootloader_opt) {
        spdlog::error("Unknown bootloader: {}", bootloader_str);
        return {};
    }

#ifdef NDEVENV
    auto result = cachyos::installer::auto_partition(device_info, system_info, *bootloader_opt, {});
    if (!result) {
        spdlog::error("{}", result.error());
        return {};
    }
    auto& partitions = *result;
#else
    const auto& boot_mountpoint = utils::bootloader_default_mount(*bootloader_opt, system_info);
    const auto& is_system_efi   = (system_info == "UEFI"sv);
    auto partitions              = gucc::disk::generate_default_partition_schema(device_info, boot_mountpoint, is_system_efi);
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
    auto* config_instance   = Config::instance();
    auto& config_data       = config_instance->data();
    const auto& device_info = std::get<std::string>(config_data["DEVICE"]);

#ifdef NDEVENV
    const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
    const auto& simple_mode   = std::get<std::int32_t>(config_data["SIMPLE_MODE"]);

    const auto task = [&](gucc::utils::SubProcess& child) -> bool {
        auto result = cachyos::installer::secure_wipe(device_info, child);
        if (!result) {
            spdlog::error("{}", result.error());
            return false;
        }
        return true;
    };
    if (simple_mode || headless_mode) {
        tui::detail::follow_process_log_task_stdout(task);
    } else {
        tui::detail::follow_process_log_task(task);
    }
#else
    spdlog::debug("{}\n", device_info);
#endif
}

void generate_fstab() noexcept {
    const auto& mountpoint = utils::get_mountpoint();
#ifdef NDEVENV
    auto result = cachyos::installer::generate_fstab(mountpoint);
    if (!result) {
        spdlog::error("Failed to generate fstab: {}", result.error());
    }
#else
    spdlog::info("Generating fstab on {}", mountpoint);
#endif
}

// Set system hostname
void set_hostname(const std::string_view& hostname) noexcept {
    spdlog::info("Setting hostname {}", hostname);
#ifdef NDEVENV
    const cachyos::installer::SystemSettings settings{.hostname = std::string{hostname}};
    auto result = cachyos::installer::apply_system_settings(settings, utils::get_mountpoint());
    if (!result) {
        spdlog::error("Failed to set hostname: {}", result.error());
    }
#endif
}

// Set system language
void set_locale(const std::string_view& locale) noexcept {
    spdlog::info("Selected locale: {}", locale);
#ifdef NDEVENV
    const cachyos::installer::SystemSettings settings{.locale = std::string{locale}};
    auto result = cachyos::installer::apply_system_settings(settings, utils::get_mountpoint());
    if (!result) { spdlog::error("Failed to set locale: {}", result.error()); }
#endif
}

void set_xkbmap(const std::string_view& xkbmap) noexcept {
    spdlog::info("Selected xkbmap: {}", xkbmap);
#ifdef NDEVENV
    const cachyos::installer::SystemSettings settings{.xkbmap = std::string{xkbmap}};
    auto result = cachyos::installer::apply_system_settings(settings, utils::get_mountpoint());
    if (!result) { spdlog::error("Failed to set xkbmap: {}", result.error()); }
#endif
}

void set_keymap(std::string_view selected_keymap) noexcept {
    spdlog::info("Selected keymap: {}", selected_keymap);

    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    config_data["KEYMAP"] = std::string{selected_keymap};

#ifdef NDEVENV
    const cachyos::installer::SystemSettings settings{.keymap = std::string{selected_keymap}};
    auto result = cachyos::installer::apply_system_settings(settings, utils::get_mountpoint());
    if (!result) { spdlog::error("Failed to set keymap: {}", result.error()); }
#endif
}

void set_timezone(const std::string_view& timezone) noexcept {
    spdlog::info("Timezone is set to {}", timezone);
#ifdef NDEVENV
    const cachyos::installer::SystemSettings settings{.timezone = std::string{timezone}};
    auto result = cachyos::installer::apply_system_settings(settings, utils::get_mountpoint());
    if (!result) { spdlog::error("Failed to set timezone: {}", result.error()); }
#endif
}

void set_hw_clock(const std::string_view& clock_type) noexcept {
    spdlog::info("Clock type is: {}", clock_type);
#ifdef NDEVENV
    cachyos::installer::SystemSettings settings{};
    if (clock_type == "utc"sv) {
        settings.hw_clock = cachyos::installer::SystemSettings::HwClock::UTC;
    } else if (clock_type == "localtime"sv) {
        settings.hw_clock = cachyos::installer::SystemSettings::HwClock::Localtime;
    } else {
        spdlog::error("Unknown clock type {}", clock_type);
        return;
    }
    auto result = cachyos::installer::apply_system_settings(settings, utils::get_mountpoint());
    if (!result) { spdlog::error("Failed to set hw clock: {}", result.error()); }
#endif
}

// Create user on the system
void create_new_user(const std::string_view& user, const std::string_view& password, const std::string_view& shell) noexcept {
    spdlog::info("default shell: [{}]", shell);

#ifdef NDEVENV
    auto* config_instance     = Config::instance();
    auto& config_data         = config_instance->data();
    const auto& mountpoint    = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& hostcache     = std::get<std::int32_t>(config_data["hostcache"]) != 0;
    const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
    const auto& simple_mode   = std::get<std::int32_t>(config_data["SIMPLE_MODE"]);

    const cachyos::installer::UserSettings settings{
        .username = std::string{user},
        .password = std::string{password},
        .shell    = std::string{shell},
    };
    const auto task = [&](gucc::utils::SubProcess& child) -> bool {
        auto result = cachyos::installer::create_user(settings, mountpoint, hostcache, child);
        if (!result) {
            spdlog::error("{}", result.error());
            return false;
        }
        return true;
    };
    if (simple_mode || headless_mode) {
        tui::detail::follow_process_log_task_stdout(task);
    } else {
        tui::detail::follow_process_log_task(task);
    }
#else
    spdlog::debug("user := {}, password := {}", user, password);
#endif
}

// Set password for root user
void set_root_password(const std::string_view& password) noexcept {
#ifdef NDEVENV
    auto result = cachyos::installer::set_root_password(password, utils::get_mountpoint());
    if (!result) {
        spdlog::error("Failed to set root password: {}", result.error());
    }
#else
    spdlog::debug("root password := {}", password);
#endif
}

// Finds all available partitions according to type(s) specified and generates a list
// of them. This also includes partitions on different devices.
void find_partitions() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    config_data["PARTITIONS"]        = {};
    config_data["NUMBER_PARTITIONS"] = 0;
    auto& number_partitions          = std::get<std::int32_t>(config_data["NUMBER_PARTITIONS"]);
    const auto& include_part         = std::get<std::string>(config_data["INCLUDE_PART"]);

    // get the list of partitions and also include the zvols since it is common to mount filesystems directly on them.  It should be safe to include them here since they present as block devices.
    static constexpr auto other_piece = R"(sed 's/part$/\/dev\//g' | sed 's/lvm$\|crypt$/\/dev\/mapper\//g' | awk '{print $3$1 " " $2}' | awk '!/mapper/{a[++i]=$0;next}1;END{while(x<length(a))print a[++x]}' ; zfs list -Ht volume -o name,volsize 2>/dev/null | awk '{printf "/dev/zvol/%s %s\n", $1, $2}')";
    const auto& partitions_tmp        = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -lno NAME,SIZE,TYPE | grep '{}' | {}"), include_part, other_piece));

    spdlog::info("found partitions:\n{}", partitions_tmp);

    // create a raid partition list
    // old_ifs="$IFS"
    // IFS=$'\n'
    // raid_partitions=($(lsblk -lno NAME,SIZE,TYPE | grep raid | awk '{print $1,$2}' | uniq))
    // IFS="$old_ifs"

    // add raid partitions to partition_list
    // for i in "${raid_partitions[@]}"
    // do
    //    partition_list="${partition_list} /dev/md/${i}"
    // done

    const auto& partition_list = gucc::utils::make_multiline(partitions_tmp, true);
    config_data["PARTITIONS"]  = partition_list;
    number_partitions          = static_cast<std::int32_t>(partition_list.size());

    // for test delete /dev:sda8
    // delete_partition_in_list "/dev/sda8"

    // Deal with partitioning schemes appropriate to mounting, lvm, and/or luks.
    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    if ((include_part == "part\\|lvm\\|crypt"sv) && (((system_info == "UEFI"sv) && (number_partitions < 2)) || ((system_info == "BIOS"sv) && (number_partitions == 0)))) {
        // Deal with incorrect partitioning for main mounting function
        static constexpr auto PartErrBody = "\nBIOS systems require a minimum of one partition (ROOT).\n \nUEFI systems require a minimum of two partitions (ROOT and UEFI).\n";
        tui::detail::msgbox_widget(PartErrBody);
        tui::create_partitions();
        return;
    } else if (include_part == "part\\|crypt"sv && (number_partitions == 0)) {
        // Ensure there is at least one partition for LVM
        static constexpr auto LvmPartErrBody = "\nThere are no viable partitions available to use Logical Volume Manager.\nA minimum of one is required.\n\nIf LVM is already in use, deactivating it will allow the partition(s)\nused for its Physical Volume(s) to be used again.\n";
        tui::detail::msgbox_widget(LvmPartErrBody);
        tui::create_partitions();
        return;
    } else if (include_part == "part\\|lvm"sv && (number_partitions < 2)) {
        // Ensure there are at least two partitions for LUKS
        static constexpr auto LuksPartErrBody = "\nA minimum of two partitions are required for encryption:\n\n1. Root (/) - standard or lvm partition types.\n\n2. Boot (/boot or /boot/efi) - standard partition types only\n(except lvm where using BIOS Grub).\n";
        tui::detail::msgbox_widget(LuksPartErrBody);
        tui::create_partitions();
        return;
    }
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

    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& hostcache  = std::get<std::int32_t>(config_data["hostcache"]) != 0;

    // Split the packages string into a vector
    auto pkg_vec = gucc::utils::make_multiline(packages, false, ' ');

#ifdef NDEVENV
    const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
    const auto& simple_mode   = std::get<std::int32_t>(config_data["SIMPLE_MODE"]);
    const auto task           = [&](gucc::utils::SubProcess& child) -> bool {
        auto result = cachyos::installer::install_packages(pkg_vec, mountpoint, hostcache, child);
        if (!result) {
            spdlog::error("{}", result.error());
            return false;
        }
        return true;
    };
    if (simple_mode || headless_mode) {
        if (!tui::detail::follow_process_log_task_stdout(task)) {
            return false;
        }
    } else {
        if (!tui::detail::follow_process_log_task(task)) {
            return false;
        }
    }
#else
    spdlog::info("Installing from pkglist: '{}'", packages);
#endif
    return true;
}

void install_base(const std::string_view& packages) noexcept {
#ifdef NDEVENV
    // Ensure crypto state is current before building context
    utils::recheck_luks();

    auto ctx   = utils::build_install_context();
    ctx.kernel = std::string{packages};

    auto* config_instance     = Config::instance();
    auto& config_data         = config_instance->data();
    const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
    const auto& simple_mode   = std::get<std::int32_t>(config_data["SIMPLE_MODE"]);

    const auto install_task = [&](gucc::utils::SubProcess& child) -> bool {
        auto result = cachyos::installer::install_base(ctx, child);
        if (!result) {
            spdlog::error("{}", result.error());
            return false;
        }
        return true;
    };
    if (simple_mode || headless_mode) {
        if (!tui::detail::follow_process_log_task_stdout(install_task)) {
            spdlog::error("Failed to install base");
        }
    } else {
        if (!tui::detail::follow_process_log_task(install_task)) {
            spdlog::error("Failed to install base");
        }
    }
    // NOTE: enable_services() is called internally by the library
#else
    spdlog::info("[install_base] packages: {}", packages);
#endif
}

void install_desktop(const std::string_view& desktop) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    config_data["DE"]     = std::string{desktop};

#ifdef NDEVENV
    // Ensure crypto state is current before building context
    utils::recheck_luks();

    auto ctx = utils::build_install_context();

    const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
    const auto& simple_mode   = std::get<std::int32_t>(config_data["SIMPLE_MODE"]);
    const auto install_task = [&](gucc::utils::SubProcess& child) -> bool {
        auto result = cachyos::installer::install_desktop(desktop, ctx, child);
        if (!result) {
            spdlog::error("{}", result.error());
            return false;
        }
        return true;
    };
    if (simple_mode || headless_mode) {
        if (!tui::detail::follow_process_log_task_stdout(install_task)) {
            spdlog::error("Failed to install desktop");
        }
    } else {
        if (!tui::detail::follow_process_log_task(install_task)) {
            spdlog::error("Failed to install desktop");
        }
    }
    // NOTE: enable_services() is called internally by the library
#else
    spdlog::info("[install_desktop] desktop: {}", desktop);
#endif
}

void remove_pkgs(const std::string_view& packages) noexcept {
    /* clang-format off */
    if (packages.empty()) { return; }
    /* clang-format on */

#ifdef NDEVENV
    auto* config_instance     = Config::instance();
    auto& config_data         = config_instance->data();
    const auto& mountpoint    = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
    const auto& simple_mode   = std::get<std::int32_t>(config_data["SIMPLE_MODE"]);

    auto pkg_vec = gucc::utils::make_multiline(packages, false, ' ');

    const auto task = [&](gucc::utils::SubProcess& child) -> bool {
        auto result = cachyos::installer::remove_packages(pkg_vec, mountpoint, child);
        if (!result) {
            spdlog::error("{}", result.error());
            return false;
        }
        return true;
    };

    if (simple_mode || headless_mode) {
        tui::detail::follow_process_log_task_stdout(task);
    } else {
        tui::detail::follow_process_log_task(task);
    }
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

    auto* config_instance     = Config::instance();
    auto& config_data         = config_instance->data();
    const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
    const auto& simple_mode   = std::get<std::int32_t>(config_data["SIMPLE_MODE"]);

    const auto task = [&](gucc::utils::SubProcess& child) -> bool {
        auto result = cachyos::installer::install_bootloader(ctx, child);
        if (!result) {
            spdlog::error("{}", result.error());
            return false;
        }
        return true;
    };
    if (simple_mode || headless_mode) {
        if (!tui::detail::follow_process_log_task_stdout(task)) {
            spdlog::error("Failed to install bootloader");
        }
    } else {
        if (!tui::detail::follow_process_log_task(task)) {
            spdlog::error("Failed to install bootloader");
        }
    }
#else
    spdlog::info("[install_bootloader] {}", gucc::bootloader::bootloader_to_string(bootloader));
#endif
}

// List partitions to be hidden from the mounting menu
std::string list_mounted() noexcept {
    const auto& devices = gucc::disk::list_block_devices();
    if (!devices) {
        spdlog::error("failed to find block devices");
        return {};
    }

    const auto mountpoint     = utils::get_mountpoint();
    const auto& mounted_names = gucc::disk::list_mounted_devices(*devices, mountpoint);
    return gucc::utils::join(mounted_names);
}

std::string list_containing_crypt() noexcept {
    return gucc::utils::exec("blkid | awk '/TYPE=\"crypto_LUKS\"/{print $1}' | sed 's/.$//'");
}

std::string list_non_crypt() noexcept {
    return gucc::utils::exec("blkid | awk '!/TYPE=\"crypto_LUKS\"/{print $1}' | sed 's/.$//'");
}

auto get_cryptroot() noexcept -> bool {
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    auto result = cachyos::installer::detect_crypto_root(mountpoint);
    if (!result) {
        spdlog::error("{}", result.error());
        return false;
    }

    const auto& state = *result;
    if (!state.is_luks) {
        return false;
    }

    config_data["LUKS"]           = 1;
    config_data["LUKS_ROOT_NAME"] = state.luks_root_name;
    config_data["LUKS_DEV"]       = state.luks_dev;

    if (!state.luks_uuid.empty()) {
        config_data["LUKS_UUID"] = state.luks_uuid;
    }
    if (state.is_lvm) {
        config_data["LVM"] = 1;
    }
    return true;
}

auto recheck_luks() noexcept -> bool {
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& uefi_mount = std::get<std::string>(config_data["UEFI_MOUNT"]);

    auto result = cachyos::installer::recheck_luks(mountpoint, uefi_mount);
    if (!result) {
        spdlog::error("{}", result.error());
        return false;
    }
    if (*result) {
        config_data["LUKS"] = 1;
    }
    return true;
}

auto get_cryptboot() noexcept -> bool {
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& uefi_mount = std::get<std::string>(config_data["UEFI_MOUNT"]);

    auto result = cachyos::installer::detect_crypto_boot(mountpoint, uefi_mount);
    if (!result) {
        spdlog::error("{}", result.error());
        return false;
    }

    const auto& state = *result;
    if (!state.is_luks) {
        return false;
    }

    config_data["LUKS"] = 1;
    if (state.is_lvm) {
        config_data["LVM"] = 1;
    }

    // Merge boot's luks_dev into existing LUKS_DEV (may already have root's entry)
    auto& existing_luks_dev = std::get<std::string>(config_data["LUKS_DEV"]);
    if (!state.luks_uuid.empty() && !existing_luks_dev.contains(state.luks_uuid)) {
        existing_luks_dev = fmt::format(FMT_COMPILE("{} {}"), existing_luks_dev, state.luks_dev);
    }
    return true;
}

auto boot_encrypted_setting() noexcept -> bool {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& uefi_mount = std::get<std::string>(config_data["UEFI_MOUNT"]);
    const auto& luks       = std::get<std::int32_t>(config_data["LUKS"]);

    config_data["fde"] = 0;

    auto result = cachyos::installer::boot_encrypted_setting(mountpoint, uefi_mount, luks == 1);
    if (!result) {
        spdlog::error("{}", result.error());
        return false;
    }
    if (*result) {
        config_data["fde"] = 1;
    }
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
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& result = cachyos::installer::detect_system();
    if (!result) {
        spdlog::error("detect_system failed: {}", result.error());
        // TODO(vnepogodin): for interactivity it shouldn't exit out forcefully
        exit(1);
    }

    if (result->system_mode == cachyos::installer::InstallContext::SystemMode::UEFI) {
        config_data["SYSTEM"] = "UEFI";
    }

    // TODO(vnepogodin): Test which nw-client is available
    if (gucc::utils::exec("systemctl is-active NetworkManager") == "active"sv) {
        config_data["NW_CMD"] = "nmtui";
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
        gucc::utils::exec("iwctl", true);
        break;
    }
}

void enable_autologin([[maybe_unused]] const std::string_view& dm, [[maybe_unused]] const std::string_view& username) noexcept {
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    auto result = cachyos::installer::enable_autologin(dm, username, mountpoint);
    if (!result) {
        spdlog::error("Failed to enable autologin: {}", result.error());
    }
#endif
}

bool parse_config() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    spdlog::info("CachyOS installer version '{}'", INSTALLER_VERSION);
    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    spdlog::info("SYSTEM: '{}'", system_info);

    // 1. Open file for reading.
    static constexpr auto file_path = "settings.json";
    const auto file_content         = gucc::file_utils::read_whole_file(file_path);

    // 2. Parse JSON using installer_config library.
    auto parse_result = installer::parse_installer_config(file_content);
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
        auto validation = installer::validate_headless_config(config);
        if (!validation) {
            fmt::print(stderr, "{}\n", validation.error());
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
        std::vector<std::string> ready_parts{};
        for (const auto& part : config.partitions) {
            auto&& part_data = fmt::format(FMT_COMPILE("{}\t{}\t{}\t{}\t{}"),
                part.name, part.mountpoint, part.size, part.fs_name,
                installer::partition_type_to_string(part.type));
            ready_parts.push_back(std::move(part_data));
        }
        config_data["READY_PARTITIONS"] = std::move(ready_parts);
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

auto setup_luks_keyfile() noexcept -> bool {
#ifdef NDEVENV
    const auto mountpoint = utils::get_mountpoint();
    auto result           = cachyos::installer::setup_luks_keyfile(mountpoint);
    if (!result) {
        spdlog::error("{}", result.error());
        return false;
    }
#endif
    return true;
}

void grub_mkconfig() noexcept {
    utils::arch_chroot("grub-mkconfig -o /boot/grub/grub.cfg");
    // check_for_error "grub-mkconfig" $?
}

void enable_services() noexcept {
#ifdef NDEVENV
    auto ctx    = utils::build_install_context();
    auto result = cachyos::installer::enable_services(ctx);
    if (!result) {
        spdlog::error("Failed to enable services: {}", result.error());
    }
#endif
}

void final_check() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    config_data["CHECKLIST"] = "";
    auto& checklist          = std::get<std::string>(config_data["CHECKLIST"]);

    // Build InstallContext from Config for the library call
    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    const auto& mountpoint  = std::get<std::string>(config_data["MOUNTPOINT"]);

    cachyos::installer::InstallContext ctx{};
    ctx.mountpoint  = mountpoint;
    ctx.system_mode = (system_info == "UEFI"sv)
        ? cachyos::installer::InstallContext::SystemMode::UEFI
        : cachyos::installer::InstallContext::SystemMode::BIOS;

    const auto& result = cachyos::installer::final_check(ctx);
    for (const auto& err : result.errors) {
        checklist += fmt::format(FMT_COMPILE("- {}\n"), err);
    }
    for (const auto& warn : result.warnings) {
        checklist += fmt::format(FMT_COMPILE("- {}\n"), warn);
    }
}

}  // namespace utils
