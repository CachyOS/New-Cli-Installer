#include "utils.hpp"
#include "config.hpp"
#include "definitions.hpp"
#include "subprocess.h"
#include "tui.hpp"
#include "widgets.hpp"

// import gucc
#include "gucc/autologin.hpp"
#include "gucc/bootloader.hpp"
#include "gucc/cpu.hpp"
#include "gucc/fetch_file.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/hwclock.hpp"
#include "gucc/initcpio.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/kernel_params.hpp"
#include "gucc/locale.hpp"
#include "gucc/luks.hpp"
#include "gucc/package_profiles.hpp"
#include "gucc/pacmanconf_repo.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/systemd_services.hpp"
#include "gucc/umount_partitions.hpp"
#include "gucc/user.hpp"

#include <cerrno>       // for errno, strerror
#include <cstdint>      // for int32_t
#include <cstdio>       // for feof, fgets, pclose, perror, popen
#include <cstdlib>      // for exit, WIFEXITED, WIFSIGNALED
#include <sys/mount.h>  // for mount

#include <algorithm>      // for transform, find, search
#include <array>          // for array
#include <bit>            // for bit_cast
#include <chrono>         // for filesystem, seconds
#include <filesystem>     // for exists, is_directory
#include <fstream>        // for ofstream
#include <iostream>       // for basic_istream, cin
#include <mutex>          // for mutex
#include <ranges>         // for ranges::*
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

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace utils {

bool is_connected() noexcept {
#ifdef NDEVENV
    /* clang-format off */
    auto r = cpr::Get(cpr::Url{"https://cachyos.org"},
             cpr::Timeout{15000}); // 15s
    /* clang-format on */
    return cpr::status::is_success(static_cast<std::int32_t>(r.status_code)) || cpr::status::is_redirect(static_cast<std::int32_t>(r.status_code));
#else
    return true;
#endif
}

bool check_root() noexcept {
#ifdef NDEVENV
    return (gucc::utils::exec("whoami") == "root");
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

    const auto& mountpoint    = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);

    const bool follow_headless = follow && headless_mode;
    if (!follow || follow_headless) {
        gucc::utils::arch_chroot(command, mountpoint, follow_headless);
    }
#ifdef NDEVENV
    else {
        const auto& cmd_formatted = fmt::format(FMT_COMPILE("arch-chroot {} {} 2>>/tmp/cachyos-install.log 2>&1"), mountpoint, command);
        tui::detail::follow_process_log_widget({"/bin/sh", "-c", cmd_formatted});
    }
#endif
}

void exec_follow(const std::vector<std::string>& vec, std::string& process_log, bool& running, subprocess_s& child, bool async) noexcept {
    if (!async) {
        spdlog::error("Implement me!");
        return;
    }

    const bool log_exec_cmds = gucc::utils::safe_getenv("LOG_EXEC_CMDS") == "1";
    const bool dirty_cmd_run = gucc::utils::safe_getenv("DIRTY_CMD_RUN") == "1";

    if (log_exec_cmds && spdlog::default_logger_raw() != nullptr) {
        spdlog::debug("[exec_follow] cmd := {}", vec);
    }
    if (dirty_cmd_run) {
        return;
    }

    std::vector<char*> args;
    std::transform(vec.cbegin(), vec.cend(), std::back_inserter(args),
        [=](const std::string& arg) -> char* { return std::bit_cast<char*>(arg.data()); });
    args.push_back(nullptr);

    static std::array<char, 1048576 + 1> data{};
    static std::mutex s_mutex{};
    std::memset(data.data(), 0, data.size());

    int32_t ret{-1};
    subprocess_s process{};
    char** command                             = args.data();
    static constexpr const char* environment[] = {"PATH=/sbin:/bin:/usr/local/sbin:/usr/local/bin:/usr/bin:/usr/sbin", nullptr};
    if ((ret = subprocess_create_ex(command, subprocess_option_enable_async | subprocess_option_combined_stdout_stderr, environment, &process)) != 0) {
        running = false;
        return;
    }

    child = process;

    std::uint32_t index{};
    std::uint32_t bytes_read{};
    do {
        const std::lock_guard<std::mutex> lock(s_mutex);

        bytes_read = subprocess_read_stdout(&process, data.data() + index,
            static_cast<std::uint32_t>(data.size()) - 1 - index);

        index += bytes_read;
        process_log = data.data();
    } while (bytes_read != 0);

    subprocess_join(&process, &ret);
    subprocess_destroy(&process);
    child   = process;
    running = false;
}

void dump_to_log(const std::string& data) noexcept {
    spdlog::info("[DUMP_TO_LOG] :=\n{}", data);
}

void dump_settings_to_log() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    std::string out{};
    for (const auto& [key, value] : config_data) {
        const auto& value_formatted = std::visit([](auto&& arg) -> std::string { return fmt::format("{}", arg); }, value);
        out += fmt::format("Option: [{}], Value: [{}]\n", key, value_formatted);
    }
    spdlog::info("Settings:\n{}", out);
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
    if (!gucc::utils::exec_checked(fmt::format(FMT_COMPILE("pacman -Qq {} &>/dev/null"), pkg))) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        utils::clear_screen();

        const auto& cmd_formatted = fmt::format(FMT_COMPILE("pacman -Sy --noconfirm {}"), pkg);

#ifdef NDEVENV
        auto* config_instance     = Config::instance();
        auto& config_data         = config_instance->data();
        const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
        if (headless_mode) {
            gucc::utils::exec(cmd_formatted, true);
        } else {
            tui::detail::follow_process_log_widget({"/bin/sh", "-c", cmd_formatted});
        }
        // gucc::utils::exec(fmt::format(FMT_COMPILE("pacman -Sy --noconfirm {}"), pkg));
#else
        spdlog::info("Installing needed: '{}'", cmd_formatted);
#endif
    }
}

// Unmount partitions.
void umount_partitions() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& mountpoint      = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& zfs_zpool_names = std::get<std::vector<std::string>>(config_data["ZFS_ZPOOL_NAMES"]);
#ifdef NDEVENV

    gucc::utils::exec("swapoff -a");

    if (!gucc::umount::umount_partitions(mountpoint, zfs_zpool_names)) {
        spdlog::error("Failed to umount partitions");
    }
#else
    spdlog::info("Unmounting partitions on {}, zfs zpool names {}", mountpoint, zfs_zpool_names);
#endif
}

// BIOS and UEFI
void auto_partition() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& device_info                  = std::get<std::string>(config_data["DEVICE"]);
    [[maybe_unused]] const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);

#ifdef NDEVENV
    // Find existing partitions (if any) to remove
    const auto& parts     = gucc::utils::exec(fmt::format(FMT_COMPILE("parted -s {} print | {}"), device_info, "awk '/^ / {print $1}'"));
    const auto& del_parts = gucc::utils::make_multiline(parts);
    for (const auto& del_part : del_parts) {
        gucc::utils::exec(fmt::format(FMT_COMPILE("parted -s {} rm {} 2>>/tmp/cachyos-install.log &>/dev/null"), device_info, del_part));
    }

    // Clear disk
    gucc::utils::exec(fmt::format(FMT_COMPILE("dd if=/dev/zero of='{}' bs=512 count=1 2>>/tmp/cachyos-install.log &>/dev/null"), device_info));
    gucc::utils::exec(fmt::format(FMT_COMPILE("wipefs -af '{}' 2>>/tmp/cachyos-install.log &>/dev/null"), device_info));
    gucc::utils::exec(fmt::format(FMT_COMPILE("sgdisk -Zo '{}' 2>>/tmp/cachyos-install.log &>/dev/null"), device_info));

    // Identify the partition table
    const auto& part_table = gucc::utils::exec(fmt::format(FMT_COMPILE("parted -s {} print 2>/dev/null | grep -i 'partition table' | {}"), device_info, "awk '{print $3}'"));

    // Create partition table if one does not already exist
    if ((system_info == "BIOS") && (part_table != "msdos")) {
        gucc::utils::exec(fmt::format(FMT_COMPILE("parted -s {} mklabel msdos 2>>/tmp/cachyos-install.log &>/dev/null"), device_info));
    }
    if ((system_info == "UEFI") && (part_table != "gpt")) {
        gucc::utils::exec(fmt::format(FMT_COMPILE("parted -s {} mklabel gpt 2>>/tmp/cachyos-install.log &>/dev/null"), device_info));
    }

    // Create partitions (same basic partitioning scheme for BIOS and UEFI)
    if (system_info == "BIOS") {
        gucc::utils::exec(fmt::format(FMT_COMPILE("parted -s {} mkpart primary ext3 1MiB 513MiB 2>>/tmp/cachyos-install.log &>/dev/null"), device_info));
    } else {
        gucc::utils::exec(fmt::format(FMT_COMPILE("parted -s {} mkpart ESP fat32 1MiB 513MiB 2>>/tmp/cachyos-install.log &>/dev/null"), device_info));
    }

    gucc::utils::exec(fmt::format(FMT_COMPILE("parted -s {} set 1 boot on 2>>/tmp/cachyos-install.log &>/dev/null"), device_info));
    gucc::utils::exec(fmt::format(FMT_COMPILE("parted -s {} mkpart primary ext3 513MiB 100% 2>>/tmp/cachyos-install.log &>/dev/null"), device_info));
#else
    spdlog::info("lsblk {} -o NAME,TYPE,FSTYPE,SIZE", device_info);
#endif
}

// Securely destroy all data on a given device.
void secure_wipe() noexcept {
    auto* config_instance   = Config::instance();
    auto& config_data       = config_instance->data();
    const auto& device_info = std::get<std::string>(config_data["DEVICE"]);

#ifdef NDEVENV
    utils::inst_needed("wipe");

    const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
    const auto& cmd_formatted = fmt::format(FMT_COMPILE("wipe -Ifre {}"), device_info);
    if (headless_mode) {
        gucc::utils::exec(cmd_formatted, true);
    } else {
        tui::detail::follow_process_log_widget({"/bin/sh", "-c", cmd_formatted});
    }
    // gucc::utils::exec(fmt::format(FMT_COMPILE("wipe -Ifre {}"), device_info));
#else
    spdlog::debug("{}\n", device_info);
#endif
}

void generate_fstab() noexcept {
    static constexpr auto fstab_cmd = "genfstab -U -p"sv;
    spdlog::info("Generating with fstab '{}'", fstab_cmd);

#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    gucc::utils::exec(fmt::format(FMT_COMPILE("{0} {1} > {1}/etc/fstab"), fstab_cmd, mountpoint));
    spdlog::info("Created fstab file:");

    const auto& fstab_content = gucc::file_utils::read_whole_file(fmt::format(FMT_COMPILE("{}/etc/fstab"), mountpoint));
    utils::dump_to_log(fstab_content);

    const auto& swap_file = fmt::format(FMT_COMPILE("{}/swapfile"), mountpoint);
    if (fs::exists(swap_file) && fs::is_regular_file(swap_file)) {
        spdlog::info("appending swapfile to the fstab..");
        gucc::utils::exec(fmt::format(FMT_COMPILE("sed -i \"s/\\\\{0}//\" {0}/etc/fstab"), mountpoint));
    }

    // Edit fstab in case of btrfs subvolumes
    gucc::utils::exec(fmt::format(FMT_COMPILE("sed -i 's/subvolid=.*,subvol=\\/.*,//g' {}/etc/fstab"), mountpoint));

    // remove any zfs datasets that are mounted by zfs
    const auto& msource_list = gucc::utils::make_multiline(gucc::utils::exec(fmt::format(FMT_COMPILE("cat {}/etc/fstab | grep '^[a-z,A-Z]' | {}"), mountpoint, "awk '{print $1}'")));
    for (const auto& msource : msource_list) {
        if (gucc::utils::exec_checked(fmt::format(FMT_COMPILE("zfs list -H -o mountpoint,name | grep '^/' | {} | grep -q '^{}$'"), "awk '{print $2}'", msource))) {
            gucc::utils::exec(fmt::format(FMT_COMPILE("sed -e '\\|^{}[[:space:]]| s/^#*/#/' -i {}/etc/fstab"), msource, mountpoint));
        }
    }
#endif
}

// Set system hostname
void set_hostname(const std::string_view& hostname) noexcept {
    spdlog::info("Setting hostname {}", hostname);
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    if (!gucc::user::set_hostname(hostname, mountpoint)) {
        spdlog::error("Failed to set hostname");
    }
#endif
}

// Set system language
void set_locale(const std::string_view& locale) noexcept {
    spdlog::info("Selected locale: {}", locale);
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    if (!gucc::locale::set_locale(locale, mountpoint)) {
        spdlog::error("Failed to set locale");
    }
#endif
}

void set_xkbmap(const std::string_view& xkbmap) noexcept {
    spdlog::info("Selected xkbmap: {}", xkbmap);
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    gucc::utils::exec(fmt::format(FMT_COMPILE("echo -e \"Section \"\\\"InputClass\"\\\"\\nIdentifier \"\\\"system-keyboard\"\\\"\\nMatchIsKeyboard \"\\\"on\"\\\"\\nOption \"\\\"XkbLayout\"\\\" \"\\\"{0}\"\\\"\\nEndSection\" > {1}/etc/X11/xorg.conf.d/00-keyboard.conf"), xkbmap, mountpoint));
#endif
}

void set_keymap(const std::string_view& selected_keymap) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    config_data["KEYMAP"] = std::string{selected_keymap};

    spdlog::info("Selected keymap: {}", selected_keymap);
}

void set_timezone(const std::string_view& timezone) noexcept {
    spdlog::info("Timezone is set to {}", timezone);
#ifdef NDEVENV
    utils::arch_chroot(fmt::format(FMT_COMPILE("ln -sf /usr/share/zoneinfo/{} /etc/localtime"), timezone), false);
#endif
}

void set_hw_clock(const std::string_view& clock_type) noexcept {
    spdlog::info("Clock type is: {}", clock_type);
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    if (clock_type == "utc"sv) {
        if (!gucc::hwclock::set_hwclock_utc(mountpoint)) {
            spdlog::error("Failed to set UTC hwclock");
        }
    } else if (clock_type == "localtime"sv) {
        if (!gucc::hwclock::set_hwclock_localtime(mountpoint)) {
            spdlog::error("Failed to set localtime hwclock");
        }
    } else {
        spdlog::error("Unknown clock type {}", clock_type);
    }
#endif
}

// Create user on the system
void create_new_user(const std::string_view& user, const std::string_view& password, const std::string_view& shell) noexcept {
    spdlog::info("default shell: [{}]", shell);

#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    if (shell.ends_with("zsh") || shell.ends_with("fish")) {
        const auto& packages  = fmt::format(FMT_COMPILE("cachyos-{}-config"), (shell.ends_with("zsh")) ? "zsh" : "fish");
        const auto& hostcache = std::get<std::int32_t>(config_data["hostcache"]);
        const auto& cmd       = (hostcache) ? "pacstrap" : "pacstrap -c";

        const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
        const auto& cmd_formatted = fmt::format(FMT_COMPILE("{} {} {} |& tee -a /tmp/pacstrap.log"), cmd, mountpoint, packages);
        if (headless_mode) {
            gucc::utils::exec(cmd_formatted, true);
        } else {
            tui::detail::follow_process_log_widget({"/bin/sh", "-c", cmd_formatted});
        }
    }

    // Create user with sudoers and default groups
    using namespace std::string_literals;
    using namespace std::string_view_literals;
    const gucc::user::UserInfo user_info{
        .username      = user,
        .password      = password,
        .shell         = shell,
        .sudoers_group = "wheel"sv,
    };
    const std::vector default_user_groups{"wheel"s, "rfkill"s, "sys"s, "users"s, "lp"s, "video"s, "network"s, "storage"s, "audio"s};
    if (!gucc::user::create_new_user(user_info, default_user_groups, mountpoint)) {
        spdlog::error("Failed to create user");
    }
#else
    spdlog::debug("user := {}, password := {}", user, password);
#endif
}

// Set password for root user
void set_root_password(const std::string_view& password) noexcept {
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    if (!gucc::user::set_root_password(password, mountpoint)) {
        spdlog::error("Failed to set root password");
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
    if ((include_part == "part\\|lvm\\|crypt") && (((system_info == "UEFI") && (number_partitions < 2)) || ((system_info == "BIOS") && (number_partitions == 0)))) {
        // Deal with incorrect partitioning for main mounting function
        static constexpr auto PartErrBody = "\nBIOS systems require a minimum of one partition (ROOT).\n \nUEFI systems require a minimum of two partitions (ROOT and UEFI).\n";
        tui::detail::msgbox_widget(PartErrBody);
        tui::create_partitions();
        return;
    } else if (include_part == "part\\|crypt" && (number_partitions == 0)) {
        // Ensure there is at least one partition for LVM
        static constexpr auto LvmPartErrBody = "\nThere are no viable partitions available to use Logical Volume Manager.\nA minimum of one is required.\n\nIf LVM is already in use, deactivating it will allow the partition(s)\nused for its Physical Volume(s) to be used again.\n";
        tui::detail::msgbox_widget(LvmPartErrBody);
        tui::create_partitions();
        return;
    } else if (include_part == "part\\|lvm" && (number_partitions < 2)) {
        // Ensure there are at least two partitions for LUKS
        static constexpr auto LuksPartErrBody = "\nA minimum of two partitions are required for encryption:\n\n1. Root (/) - standard or lvm partition types.\n\n2. Boot (/boot or /boot/efi) - standard partition types only\n(except lvm where using BIOS Grub).\n";
        tui::detail::msgbox_widget(LuksPartErrBody);
        tui::create_partitions();
        return;
    }
}

void lvm_detect(std::optional<std::function<void()>> func_callback) noexcept {
    const auto& lvm_pv = gucc::utils::exec("pvs -o pv_name --noheading 2>/dev/null");
    const auto& lvm_vg = gucc::utils::exec("vgs -o vg_name --noheading 2>/dev/null");
    const auto& lvm_lv = gucc::utils::exec("lvs -o vg_name,lv_name --noheading --separator - 2>/dev/null");

    if (lvm_lv.empty() || lvm_vg.empty() || lvm_pv.empty()) {
        return;
    }

    if (func_callback.has_value()) {
        func_callback.value()();
    }

#ifdef NDEVENV
    gucc::utils::exec("modprobe dm-mod");
    gucc::utils::exec("vgscan >/dev/null 2>&1");
    gucc::utils::exec("vgchange -ay >/dev/null 2>&1");
#endif
}

auto get_pkglist_base(const std::string_view& packages) noexcept -> std::optional<std::vector<std::string>> {
    auto* config_instance              = Config::instance();
    auto& config_data                  = config_instance->data();
    const auto& server_mode            = std::get<std::int32_t>(config_data["SERVER_MODE"]);
    const auto& mountpoint_info        = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& net_profs_url          = std::get<std::string>(config_data["NET_PROFILES_URL"]);
    const auto& net_profs_fallback_url = std::get<std::string>(config_data["NET_PROFILES_FALLBACK_URL"]);

    const auto& root_filesystem     = gucc::fs::utils::get_mountpoint_fs(mountpoint_info);
    const auto& is_root_on_zfs      = (root_filesystem == "zfs");
    const auto& is_root_on_btrfs    = (root_filesystem == "btrfs");
    const auto& is_root_on_bcachefs = (root_filesystem == "bcachefs");

    auto pkg_list = gucc::utils::make_multiline(packages, false, ' ');

    const auto pkg_count = pkg_list.size();
    for (std::size_t i = 0; i < pkg_count; ++i) {
        const auto& pkg = pkg_list[i];
        pkg_list.emplace_back(fmt::format(FMT_COMPILE("{}-headers"), pkg));
        if (is_root_on_zfs && pkg.starts_with("linux-cachyos")) {
            pkg_list.emplace_back(fmt::format(FMT_COMPILE("{}-zfs"), pkg));
        }
    }
    if (is_root_on_zfs) {
        pkg_list.insert(pkg_list.cend(), {"zfs-utils"});
    }
    if (is_root_on_bcachefs) {
        pkg_list.insert(pkg_list.cend(), {"bcachefs-tools"});
    }

    auto net_profs_content = gucc::fetch::fetch_file_from_url(net_profs_url, net_profs_fallback_url);
    if (!net_profs_content) {
        spdlog::error("Failed to get net profiles content");
        return std::nullopt;
    }
    auto base_net_profs = gucc::profile::parse_base_profiles(*net_profs_content);
    if (!base_net_profs.has_value()) {
        spdlog::error("Failed to parse net profiles");
        return std::nullopt;
    }

    if (server_mode == 0) {
        if (is_root_on_btrfs) {
            pkg_list.insert(pkg_list.cend(), {"snapper", "btrfs-assistant-git"});
        }
        pkg_list.insert(pkg_list.cend(),
            base_net_profs->base_desktop_packages.begin(),
            base_net_profs->base_desktop_packages.end());
    }
    pkg_list.insert(pkg_list.cend(),
        base_net_profs->base_packages.begin(),
        base_net_profs->base_packages.end());

    return std::make_optional<std::vector<std::string>>(pkg_list);
}

auto get_pkglist_desktop(const std::string_view& desktop_env) noexcept -> std::optional<std::vector<std::string>> {
    auto* config_instance              = Config::instance();
    auto& config_data                  = config_instance->data();
    const auto& net_profs_url          = std::get<std::string>(config_data["NET_PROFILES_URL"]);
    const auto& net_profs_fallback_url = std::get<std::string>(config_data["NET_PROFILES_FALLBACK_URL"]);

    auto net_profs_content = gucc::fetch::fetch_file_from_url(net_profs_url, net_profs_fallback_url);
    if (!net_profs_content) {
        spdlog::error("Failed to get net profiles content");
        return std::nullopt;
    }
    auto desktop_net_profs = gucc::profile::parse_desktop_profiles(*net_profs_content);
    if (!desktop_net_profs.has_value()) {
        spdlog::error("Failed to parse net profiles");
        return std::nullopt;
    }

    std::vector<std::string> pkg_list{};

    constexpr std::string_view kde{"kde"};
    constexpr std::string_view xfce{"xfce"};
    constexpr std::string_view sway{"sway"};
    constexpr std::string_view wayfire{"wayfire"};
    constexpr std::string_view i3wm{"i3wm"};
    constexpr std::string_view gnome{"gnome"};
    constexpr std::string_view openbox{"openbox"};
    constexpr std::string_view bspwm{"bspwm"};
    constexpr std::string_view lxqt{"lxqt"};
    constexpr std::string_view cinnamon{"cinnamon"};
    constexpr std::string_view ukui{"ukui"};
    constexpr std::string_view qtile{"qtile"};
    constexpr std::string_view mate{"mate"};
    constexpr std::string_view lxde{"lxde"};
    constexpr std::string_view hyprland{"hyprland"};
    constexpr std::string_view budgie{"budgie"};

    bool needed_xorg{};
    auto found = std::ranges::search(desktop_env, i3wm);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, i3wm, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, sway);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, sway, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
    }
    found = std::ranges::search(desktop_env, kde);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, kde, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, xfce);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, xfce, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, gnome);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, gnome, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, wayfire);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, wayfire, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
    }
    found = std::ranges::search(desktop_env, openbox);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, openbox, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, lxqt);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, lxqt, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, bspwm);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, bspwm, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, cinnamon);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, cinnamon, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, ukui);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, ukui, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, qtile);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, qtile, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, mate);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, mate, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, lxde);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, lxde, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, hyprland);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, hyprland, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
    }
    found = std::ranges::search(desktop_env, budgie);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, budgie, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }

    if (needed_xorg) {
        auto profile = std::ranges::find(*desktop_net_profs, "xorg"sv, &gucc::profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
    }

    return std::make_optional<std::vector<std::string>>(pkg_list);
}

void install_from_pkglist(const std::string_view& packages) noexcept {
    auto* config_instance     = Config::instance();
    auto& config_data         = config_instance->data();
    const auto& mountpoint    = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& hostcache     = std::get<std::int32_t>(config_data["hostcache"]);
    const auto& cmd           = (hostcache) ? "pacstrap" : "pacstrap -c";
    const auto& cmd_formatted = fmt::format(FMT_COMPILE("{} {} {} |& tee -a /tmp/pacstrap.log"), cmd, mountpoint, packages);

#ifdef NDEVENV
    const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
    if (headless_mode) {
        gucc::utils::exec(cmd_formatted, true);
    } else {
        tui::detail::follow_process_log_widget({"/bin/sh", "-c", cmd_formatted});
    }
#else
    spdlog::info("Installing from pkglist: '{}'", cmd_formatted);
#endif
}

void install_base(const std::string_view& packages) noexcept {
    const auto& pkg_list = utils::get_pkglist_base(packages);
    if (!pkg_list.has_value()) {
        spdlog::error("Failed to install base");
        return;
    }
    const auto& base_pkgs = gucc::utils::join(*pkg_list, ' ');

    spdlog::info("Preparing for pkgs to install: '{}'", base_pkgs);

#ifdef NDEVENV
    // Prep variables
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& zfs        = std::get<std::int32_t>(config_data["ZFS"]);

    const auto& initcpio_filename = fmt::format(FMT_COMPILE("{}/etc/mkinitcpio.conf"), mountpoint);
    auto initcpio                 = gucc::detail::Initcpio{initcpio_filename};

    // NOTE: make sure that we have valid initcpio config,
    // overwise we will end up here with unbootable system.
    initcpio.append_hooks({"base", "udev", "autodetect", "modconf", "block", "filesystems", "keyboard", "fsck"});

    // filter_packages
    utils::install_from_pkglist(base_pkgs);

    fs::copy_file("/etc/pacman.conf", fmt::format(FMT_COMPILE("{}/etc/pacman.conf"), mountpoint), fs::copy_options::overwrite_existing);

    static constexpr auto base_installed = "/mnt/.base_installed";
    std::ofstream{base_installed};  // NOLINT

    // mkinitcpio handling for specific filesystems
    std::int32_t btrfs_root = 0;
    std::int32_t zfs_root   = 0;

    const auto& filesystem_type = gucc::fs::utils::get_mountpoint_fs(mountpoint);
    spdlog::info("filesystem type on '{}' := '{}'", mountpoint, filesystem_type);
    if (filesystem_type == "btrfs"sv) {
        btrfs_root = 1;
        initcpio.remove_hook("fsck");
        initcpio.append_module("btrfs");
    } else if (filesystem_type == "zfs"sv) {
        zfs_root = 1;
        initcpio.remove_hook("fsck");
        initcpio.insert_hook("filesystems", "zfs");
        initcpio.append_file("\"/usr/lib/libgcc_s.so.1\"");
    }

    utils::recheck_luks();

    // add luks and lvm hooks as needed
    const auto& lvm  = std::get<std::int32_t>(config_data["LVM"]);
    const auto& luks = std::get<std::int32_t>(config_data["LUKS"]);
    spdlog::info("LVM := {}, LUKS := {}", lvm, luks);

    if (lvm == 1 && luks == 0) {
        initcpio.insert_hook("filesystems", "lvm2");
        spdlog::info("add lvm2 hook");
    } else if (lvm == 0 && luks == 1) {
        initcpio.insert_hook("keyboard", std::vector<std::string>{"consolefont", "keymap"});
        initcpio.insert_hook("filesystems", "encrypt");
        spdlog::info("add luks hook");
    } else if (lvm == 1 && luks == 1) {
        initcpio.insert_hook("keyboard", std::vector<std::string>{"consolefont", "keymap"});
        initcpio.insert_hook("filesystems", std::vector<std::string>{"encrypt", "lvm2"});
        spdlog::info("add lvm/luks hooks");
    }

    // Just explicitly flush the data to file,
    // if smth really happened between our calls.
    if (initcpio.parse_file()) {
        initcpio.write();
    }

    if (lvm + luks + btrfs_root + zfs_root > 0) {
        utils::arch_chroot("mkinitcpio -P");
        spdlog::info("re-run mkinitcpio");
    }

    utils::generate_fstab();

    /* clang-format off */
    if (zfs == 0) { return; }
    /* clang-format on */

    // if we are using a zfs we should enable the zfs services
    for (auto&& service_name : {"zfs.target"sv, "zfs-import-cache"sv, "zfs-mount"sv, "zfs-import.target"sv}) {
        gucc::services::enable_systemd_service(service_name, mountpoint);
    }

    // we also need create the cachefile
    gucc::utils::exec(fmt::format(FMT_COMPILE("zpool set cachefile=/etc/zfs/zpool.cache $(findmnt {} -lno SOURCE | {}) 2>>/tmp/cachyos-install.log"), mountpoint, "awk -F / '{print $1}'"), true);
    gucc::utils::exec(fmt::format(FMT_COMPILE("cp /etc/zfs/zpool.cache {}/etc/zfs/zpool.cache 2>>/tmp/cachyos-install.log"), mountpoint), true);
#endif
}

void install_desktop(const std::string_view& desktop) noexcept {
    // Create the base list of packages
    const auto& pkg_list = utils::get_pkglist_desktop(desktop);
    if (!pkg_list.has_value()) {
        spdlog::error("Failed to install desktop");
        return;
    }
    const auto& packages = gucc::utils::join(*pkg_list, ' ');

    spdlog::info("Preparing for desktop envs to install: '{}'", packages);
    utils::install_from_pkglist(packages);

    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    config_data["DE"]     = std::string{desktop.data()};
    utils::enable_services();
}

void remove_pkgs(const std::string_view& packages) noexcept {
    /* clang-format off */
    if (packages.empty()) { return; }
    /* clang-format on */

#ifdef NDEVENV
    utils::arch_chroot(fmt::format(FMT_COMPILE("pacman -Rsn {}"), packages));
#endif
}

void install_grub_uefi(const std::string_view& bootid, bool as_default) noexcept {
#ifdef NDEVENV
    fs::create_directory("/mnt/hostlvm");
    gucc::utils::exec("mount --bind /run/lvm /mnt/hostlvm");
#endif

    // if root is encrypted, amend /etc/default/grub
    const auto& root_name   = gucc::utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    const auto& root_device = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/{}/,/disk/p' | {}"), root_name, "awk '/disk/ {print $1}'"));
    const auto& root_part   = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/{}/,/part/p' | {} | tr -cd '[:alnum:]'"), root_name, "awk '/part/ {print $1}'"));
#ifdef NDEVENV
    utils::boot_encrypted_setting();
#endif

    spdlog::info("root_name: {}. root_device: {}. root_part: {}", root_name, root_device, root_part);
    spdlog::info("Boot ID: {}", bootid);
    spdlog::info("Set as default: {}", as_default);

#ifdef NDEVENV
    auto* config_instance           = Config::instance();
    auto& config_data               = config_instance->data();
    const auto& uefi_mount          = std::get<std::string>(config_data["UEFI_MOUNT"]);
    const auto& mountpoint          = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& luks_dev            = std::get<std::string>(config_data["LUKS_DEV"]);
    const auto& grub_installer_path = fmt::format(FMT_COMPILE("{}/usr/bin/grub_installer.sh"), mountpoint);

    gucc::bootloader::GrubConfig grub_config_struct{};
    gucc::bootloader::GrubInstallConfig grub_install_config_struct{};

    grub_install_config_struct.is_efi        = true;
    grub_install_config_struct.do_recheck    = true;
    grub_install_config_struct.efi_directory = uefi_mount;
    grub_install_config_struct.bootloader_id = bootid;

    const auto& root_part_fs = gucc::fs::utils::get_mountpoint_fs(mountpoint);

    // grub config changes for zfs root
    if (root_part_fs == "zfs"sv) {
        // zfs needs ZPOOL_VDEV_NAME_PATH set to properly find the device
        gucc::utils::exec(fmt::format(FMT_COMPILE("echo 'ZPOOL_VDEV_NAME_PATH=YES' >> {}/etc/environment"), mountpoint));

        grub_install_config_struct.is_root_on_zfs = true;

        // zfs is considered a sparse filesystem so we can't use SAVEDEFAULT
        grub_config_struct.savedefault = std::nullopt;

        // we need to tell grub where the zfs root is
        const auto& mountpoint_source            = gucc::fs::utils::get_mountpoint_source(mountpoint);
        const auto& zroot_var                    = fmt::format(FMT_COMPILE("zfs={} rw"), mountpoint_source);
        grub_config_struct.cmdline_linux_default = fmt::format(FMT_COMPILE("{} {}"), grub_config_struct.cmdline_linux_default, zroot_var);
        grub_config_struct.cmdline_linux         = fmt::format(FMT_COMPILE("{} {}"), grub_config_struct.cmdline_linux, zroot_var);

        constexpr auto bash_code = R"(#!/bin/bash
ln -s /hostlvm /run/lvm
pacman -S --noconfirm --needed grub efibootmgr dosfstools
)";
        std::ofstream grub_installer{grub_installer_path};
        grub_installer << bash_code;
    } else {
        // we need to disable SAVEDEFAULT if either we are on LVM or BTRFS
        const auto is_root_lvm = gucc::utils::exec_checked("lsblk -ino TYPE,MOUNTPOINT | grep ' /$' | grep -q lvm");
        if (is_root_lvm || (root_part_fs == "btrfs"sv)) {
            grub_config_struct.savedefault = std::nullopt;
        }

        constexpr auto bash_code = R"(#!/bin/bash
ln -s /hostlvm /run/lvm
pacman -S --noconfirm --needed grub efibootmgr dosfstools grub-btrfs grub-hook
)";
        std::ofstream grub_installer{grub_installer_path};
        grub_installer << bash_code;
    }

    fs::permissions(grub_installer_path,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add);

    // if the device is removable append removable to the grub-install
    const auto& removable = gucc::utils::exec(fmt::format(FMT_COMPILE("cat /sys/block/{}/removable"), root_device));
    if (utils::to_int(removable) == 1) {
        grub_install_config_struct.is_removable = true;
    }

    // If the root is on btrfs-subvolume, amend grub installation
    auto is_btrfs_subvol = gucc::utils::exec_checked("mount | awk '$3 == \"/mnt\" {print $0}' | grep btrfs | grep -qv subvolid=5");
    if (!is_btrfs_subvol) {
        gucc::utils::exec(fmt::format(FMT_COMPILE("sed -e 's/ grub-btrfs//g' -i {}"), grub_installer_path));
    }
    // If encryption used amend grub
    if (!luks_dev.empty()) {
        const auto& luks_dev_formatted   = gucc::utils::exec(fmt::format(FMT_COMPILE("echo '{}' | {}"), luks_dev, "awk '{print $1}'"));
        grub_config_struct.cmdline_linux = fmt::format(FMT_COMPILE("{} {}"), luks_dev_formatted, grub_config_struct.cmdline_linux);
        spdlog::info("adding kernel parameter {}", luks_dev);
    }

    // If Full disk encryption is used, use a keyfile
    const auto& fde = std::get<std::int32_t>(config_data["fde"]);
    if (fde == 1) {
        spdlog::info("Full disk encryption enabled");
        grub_config_struct.enable_cryptodisk = true;
    }

    std::error_code err{};

    // install grub
    utils::arch_chroot("grub_installer.sh");

    if (!gucc::bootloader::install_grub(grub_config_struct, grub_install_config_struct, mountpoint)) {
        spdlog::error("Failed to install grub");
        umount("/mnt/hostlvm");
        fs::remove("/mnt/hostlvm", err);
        tui::detail::infobox_widget("\nFailed to install grub\n");
        return;
    }
    umount("/mnt/hostlvm");
    fs::remove("/mnt/hostlvm", err);

    // the grub_installer is no longer needed
    fs::remove(grub_installer_path, err);

    /* clang-format off */
    if (!as_default) { return; }
    /* clang-format on */

    utils::arch_chroot(fmt::format(FMT_COMPILE("mkdir -p {}/EFI/boot"), uefi_mount), false);
    spdlog::info("Grub efi binary status:(EFI/cachyos/grubx64.efi): {}", fs::exists(fmt::format(FMT_COMPILE("{0}/EFI/cachyos/grubx64.efi"), uefi_mount)));
    utils::arch_chroot(fmt::format(FMT_COMPILE("cp -r {0}/EFI/cachyos/grubx64.efi {0}/EFI/boot/bootx64.efi"), uefi_mount), false);
#endif
}

void install_refind() noexcept {
    spdlog::info("Installing refind...");
#ifdef NDEVENV
    auto* config_instance      = Config::instance();
    auto& config_data          = config_instance->data();
    const auto& mountpoint     = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& uefi_mount     = std::get<std::string>(config_data["UEFI_MOUNT"]);
    const auto& luks           = std::get<std::int32_t>(config_data["LUKS"]);
    const auto& luks_root_name = std::get<std::string>(config_data["LUKS_ROOT_NAME"]);
    const auto& luks_uuid      = std::get<std::string>(config_data["LUKS_UUID"]);
    const auto& luks_dev       = std::get<std::string>(config_data["LUKS_DEV"]);

    utils::inst_needed("refind");

    const auto& boot_mountpoint = fmt::format(FMT_COMPILE("{}{}"), mountpoint, uefi_mount);

    // TODO(vnepogodin): make these configurable
    static constexpr auto default_kernel_params = "quiet zswap.enabled=0 nowatchdog"sv;
    const std::vector<std::string> extra_kernel_versions{
        "linux-cachyos", "linux", "linux-cachyos-cfs", "linux-cachyos-bore",
        "linux-cachyos-tt", "linux-cachyos-bmq", "linux-cachyos-pds", "linux-cachyos-lts"};

    // TODO(vnepogodin): detection MUST NOT be done here, and instead MUST be carried from upper functions
    // ---- start ---

    // Check if the volume is removable. If so, install all drivers
    const auto& root_name   = gucc::utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    const auto& root_device = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/{}/,/disk/p' | {}"), root_name, "awk '/disk/ {print $1}'"));
    spdlog::info("root_name: {}. root_device: {}", root_name, root_device);

    std::vector<gucc::fs::Partition> partitions{};

    const auto& part_fs   = gucc::fs::utils::get_mountpoint_fs(mountpoint);
    auto root_part_struct = gucc::fs::Partition{.fstype = part_fs, .mountpoint = "/", .device = fmt::format(FMT_COMPILE("/dev/{}"), root_name)};

    const auto& root_part_uuid = gucc::fs::utils::get_device_uuid(root_part_struct.device);
    root_part_struct.uuid_str  = root_part_uuid;

    const bool is_removable = (gucc::utils::exec(fmt::format(FMT_COMPILE("cat /sys/block/{}/removable"), root_device)) == "1"sv);

    // Set subvol field in case ROOT on btrfs subvolume
    if ((root_part_struct.fstype == "btrfs"sv) && gucc::utils::exec_checked("mount | awk '$3 == \"/mnt\" {print $0}' | grep btrfs | grep -qv subvolid=5")) {
        const auto& root_subvol    = gucc::utils::exec("mount | awk '$3 == \"/mnt\" {print $6}' | sed 's/^.*subvol=/subvol=/' | sed -e 's/,.*$/,/p' | sed 's/)//g' | sed 's/subvol=//'");
        root_part_struct.subvolume = root_subvol;

        spdlog::debug("root btrfs subvol := '{}'", *root_part_struct.subvolume);
    }

    if (!luks_root_name.empty()) {
        root_part_struct.luks_mapper_name = luks_root_name;
        spdlog::debug("refind: luks_mapper_name:='{}'", *root_part_struct.luks_mapper_name);
    }
    if (!luks_uuid.empty()) {
        root_part_struct.luks_uuid = luks_uuid;
        spdlog::debug("refind: luks_uuid:='{}'", *root_part_struct.luks_uuid);
    }
    if (!luks_dev.empty()) {
        spdlog::debug("refind: luks_dev:='{}'. why we need that here???", luks_dev);
    }

    // LUKS and lvm with LUKS
    if (luks == 1) {
        const auto& luks_mapper_name      = gucc::utils::exec("mount | awk '/\\/mnt / {print $1}' | sed 's/\\/dev\\/mapper\\///'");
        root_part_struct.luks_mapper_name = luks_mapper_name;
        spdlog::debug("refind: luks_mapper_name:='{}'", *root_part_struct.luks_mapper_name);
    }
    // Lvm without LUKS
    else if (gucc::utils::exec("lsblk -i | sed -r 's/^[^[:alnum:]]+//' | grep '/mnt$' | awk '{print $6}'") == "lvm") {
        const auto& luks_mapper_name      = gucc::utils::exec("mount | awk '/\\/mnt / {print $1}' | sed 's/\\/dev\\/mapper\\///'");
        root_part_struct.luks_mapper_name = luks_mapper_name;
        spdlog::debug("refind: luks_mapper_name:='{}'", *root_part_struct.luks_mapper_name);
    }

    // insert root partition
    partitions.emplace_back(std::move(root_part_struct));
    // ---- end ---

    const auto& kernel_params = gucc::fs::get_kernel_params(partitions, default_kernel_params);
    if (!kernel_params.has_value()) {
        spdlog::error("Failed to get kernel params for partition scheme");
        return;
    }

    // start refind install & configuration
    const gucc::bootloader::RefindInstallConfig refind_install_config{
        .is_removable          = is_removable,
        .root_mountpoint       = mountpoint,
        .boot_mountpoint       = boot_mountpoint,
        .extra_kernel_versions = extra_kernel_versions,
        .kernel_params         = *kernel_params,
    };
    if (!gucc::bootloader::install_refind(refind_install_config)) {
        spdlog::error("Failed to install refind");
        return;
    }

    spdlog::info("Created rEFInd config:");
    const auto& refind_conf_content = gucc::file_utils::read_whole_file(fmt::format(FMT_COMPILE("{}/boot/refind_linux.conf"), mountpoint));
    utils::dump_to_log(refind_conf_content);

    utils::install_from_pkglist("refind-theme-nord");
#endif
    spdlog::info("Refind was succesfully installed");
}

void install_systemd_boot() noexcept {
    spdlog::info("Installing systemd-boot...");
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& uefi_mount = std::get<std::string>(config_data["UEFI_MOUNT"]);

    // preinstall systemd-boot-manager. it has to be installed
    utils::install_from_pkglist("systemd-boot-manager");

    // Check if the volume is removable

    // NOTE: for /mnt on /dev/mapper/cryptroot `root_name` will be cryptroot
    const auto& root_name = gucc::utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");

    // NOTE: for /mnt on /dev/mapper/cryptroot on /dev/sda2 with `root_name`=cryptroot, `root_device` will be sda
    const auto& root_device = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/{}/,/disk/p' | {}"), root_name, "awk '/disk/ {print $1}'"));
    spdlog::info("root_name: {}. root_device: {}", root_name, root_device);
    const auto& removable          = gucc::utils::exec(fmt::format(FMT_COMPILE("cat /sys/block/{}/removable"), root_device));
    const bool is_volume_removable = (utils::to_int(removable) == 1);

    // start systemd-boot install & configuration
    if (!gucc::bootloader::install_systemd_boot(mountpoint, uefi_mount, is_volume_removable)) {
        spdlog::error("Failed to install systemd-boot");
        return;
    }
#endif
    spdlog::info("Systemd-boot was installed");
}

void uefi_bootloader(const std::string_view& bootloader) noexcept {
#ifdef NDEVENV
    // Ensure again that efivarfs is mounted
    static constexpr auto efi_path = "/sys/firmware/efi/";
    if (fs::exists(efi_path) && fs::is_directory(efi_path)) {
        // Mount efivarfs if it is not already mounted
        const auto& mount_out = gucc::utils::exec("mount | grep /sys/firmware/efi/efivars");
        if (mount_out.empty()) {
            if (mount("efivarfs", "/sys/firmware/efi/efivars", "efivarfs", 0, "") != 0) {
                perror("utils::uefi_bootloader");
                exit(1);
            }
        }
    }
#endif

    if (bootloader == "grub") {
        utils::install_grub_uefi("cachyos");
    } else if (bootloader == "refind") {
        utils::install_refind();
    } else if (bootloader == "systemd-boot") {
        utils::install_systemd_boot();
    } else {
        spdlog::error("Unknown bootloader!");
    }
}

void bios_bootloader(const std::string_view& bootloader) noexcept {
    spdlog::info("Installing bios bootloader '{}'...", bootloader);
#ifdef NDEVENV
    // if root is encrypted, amend /etc/default/grub
    utils::boot_encrypted_setting();

    auto* config_instance    = Config::instance();
    auto& config_data        = config_instance->data();
    const auto& lvm          = std::get<std::int32_t>(config_data["LVM"]);
    const auto& lvm_sep_boot = std::get<std::int32_t>(config_data["LVM_SEP_BOOT"]);
    const auto& luks_dev     = std::get<std::string>(config_data["LUKS_DEV"]);
    const auto& mountpoint   = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& device_info  = std::get<std::string>(config_data["DEVICE"]);

    gucc::bootloader::GrubConfig grub_config_struct{};
    gucc::bootloader::GrubInstallConfig grub_install_config_struct{};

    grub_install_config_struct.is_efi     = false;
    grub_install_config_struct.do_recheck = true;

    const auto& root_part_fs = gucc::fs::utils::get_mountpoint_fs(mountpoint);

    // if /boot is LVM (whether using a seperate /boot mount or not), amend grub
    if ((lvm == 1 && lvm_sep_boot == 0) || lvm_sep_boot == 2) {
        grub_config_struct.preload_modules = fmt::format(FMT_COMPILE("lvm {}"), grub_config_struct.preload_modules);
        grub_config_struct.savedefault     = std::nullopt;
    }

    // If root is on btrfs volume, amend grub
    if (root_part_fs == "btrfs"sv) {
        grub_config_struct.savedefault = std::nullopt;
    }

    // Same setting is needed for LVM
    if (lvm == 1) {
        grub_config_struct.savedefault = std::nullopt;
    }

    const auto& grub_installer_path = fmt::format(FMT_COMPILE("{}/usr/bin/grub_installer.sh"), mountpoint);

    // grub config changes for zfs root
    if (root_part_fs == "zfs"sv) {
        // zfs needs ZPOOL_VDEV_NAME_PATH set to properly find the device
        gucc::utils::exec(fmt::format(FMT_COMPILE("echo 'ZPOOL_VDEV_NAME_PATH=YES' >> {}/etc/environment"), mountpoint));

        // zfs is considered a sparse filesystem so we can't use SAVEDEFAULT
        if (root_part_fs == "btrfs") {
            grub_config_struct.savedefault = std::nullopt;
        }

        // we need to tell grub where the zfs root is
        const auto& mountpoint_source            = gucc::fs::utils::get_mountpoint_source(mountpoint);
        const auto& zroot_var                    = fmt::format(FMT_COMPILE("zfs={} rw"), mountpoint_source);
        grub_config_struct.cmdline_linux_default = fmt::format(FMT_COMPILE("{} {}"), grub_config_struct.cmdline_linux_default, zroot_var);
        grub_config_struct.cmdline_linux         = fmt::format(FMT_COMPILE("{} {}"), grub_config_struct.cmdline_linux, zroot_var);

        constexpr auto bash_code = R"(#!/bin/bash
ln -s /hostlvm /run/lvm
pacman -S --noconfirm --needed grub os-prober
)";
        std::ofstream grub_installer{grub_installer_path};
        grub_installer << bash_code;
    } else {
        // we need to disable SAVEDEFAULT if either we are on LVM or BTRFS
        const auto is_root_lvm = gucc::utils::exec_checked("lsblk -ino TYPE,MOUNTPOINT | grep ' /$' | grep -q lvm");
        if (is_root_lvm || (root_part_fs == "btrfs")) {
            grub_config_struct.savedefault = std::nullopt;
        }

        constexpr auto bash_code = R"(#!/bin/bash
ln -s /hostlvm /run/lvm
pacman -S --noconfirm --needed grub os-prober grub-btrfs grub-hook
)";
        std::ofstream grub_installer{grub_installer_path};
        grub_installer << bash_code;
    }

    // If the root is on btrfs-subvolume, amend grub installation
    auto is_btrfs_subvol = gucc::utils::exec_checked("mount | awk '$3 == \"/mnt\" {print $0}' | grep btrfs | grep -qv subvolid=5");
    if (!is_btrfs_subvol) {
        gucc::utils::exec(fmt::format(FMT_COMPILE("sed -e 's/ grub-btrfs//g' -i {}"), grub_installer_path));
    }

    // If encryption used amend grub
    if (!luks_dev.empty()) {
        const auto& luks_dev_formatted   = gucc::utils::exec(fmt::format(FMT_COMPILE("echo '{}' | {}"), luks_dev, "awk '{print $1}'"));
        grub_config_struct.cmdline_linux = fmt::format(FMT_COMPILE("{} {}"), luks_dev_formatted, grub_config_struct.cmdline_linux);
        spdlog::info("adding kernel parameter {}", luks_dev);
    }

    // If Full disk encryption is used, use a keyfile
    const auto& fde = std::get<std::int32_t>(config_data["fde"]);
    if (fde == 1) {
        spdlog::info("Full disk encryption enabled");
        grub_config_struct.enable_cryptodisk = true;
    }

    // Remove os-prober if not selected
    constexpr std::string_view needle{"os-prober"};
    const auto& found = std::ranges::search(bootloader, needle);
    if (found.empty()) {
        gucc::utils::exec(fmt::format(FMT_COMPILE("sed -e 's/ os-prober//g' -i {}"), grub_installer_path));
    }

    std::error_code err{};

    fs::permissions(grub_installer_path,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add);

    tui::detail::infobox_widget("\nPlease wait...\n");
    gucc::utils::exec(fmt::format(FMT_COMPILE("dd if=/dev/zero of={} seek=1 count=2047"), device_info));
    fs::create_directory("/mnt/hostlvm", err);
    gucc::utils::exec("mount --bind /run/lvm /mnt/hostlvm");

    // install grub
    utils::arch_chroot("grub_installer.sh");

    // the grub_installer is no longer needed - there still needs to be a better way to do this
    fs::remove(grub_installer_path, err);

    umount("/mnt/hostlvm");
    fs::remove("/mnt/hostlvm", err);

    if (!gucc::bootloader::install_grub(grub_config_struct, grub_install_config_struct, mountpoint)) {
        spdlog::error("Failed to install grub");
        tui::detail::infobox_widget("\nFailed to install grub\n");
        return;
    }
#endif
}

void install_bootloader(const std::string_view& bootloader) noexcept {
    /* clang-format off */
    if (!utils::check_base()) { return; }
    /* clang-format on */

    auto* config_instance   = Config::instance();
    auto& config_data       = config_instance->data();
    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    if (system_info == "BIOS") {
        utils::bios_bootloader(bootloader);
    } else {
        utils::uefi_bootloader(bootloader);
    }
}

// List partitions to be hidden from the mounting menu
std::string list_mounted() noexcept {
    gucc::utils::exec("lsblk -l | awk '$7 ~ /mnt/ {print $1}' > /tmp/.mounted");
    return gucc::utils::exec("echo /dev/* /dev/mapper/* | xargs -n1 2>/dev/null | grep -f /tmp/.mounted");
}

std::string list_containing_crypt() noexcept {
    return gucc::utils::exec("blkid | awk '/TYPE=\"crypto_LUKS\"/{print $1}' | sed 's/.$//'");
}

std::string list_non_crypt() noexcept {
    return gucc::utils::exec("blkid | awk '!/TYPE=\"crypto_LUKS\"/{print $1}' | sed 's/.$//'");
}

void get_cryptroot() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    // Identify if /mnt or partition is type "crypt" (LUKS on LVM, or LUKS alone)
    if (!gucc::utils::exec_checked("lsblk | sed -r 's/^[^[:alnum:]]+//' | awk '/\\/mnt$/ {print $6}' | grep -q crypt")
        || !gucc::utils::exec_checked(R"(lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/\/mnt$/,/part/p' | awk '{print $6}' | grep -q crypt)")) {
        return;
    }

    config_data["LUKS"] = 1;
    auto& luks_name     = std::get<std::string>(config_data["LUKS_ROOT_NAME"]);
    luks_name           = gucc::utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    // Get the name of the Luks device
    if (!gucc::utils::exec_checked("lsblk -i | grep -q -e 'crypt /mnt'")) {
        // Mountpoint is not directly on LUKS device, so we need to get the crypt device above the mountpoint
        luks_name = gucc::utils::exec(R"(lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/\/mnt$/,/crypt/p' | awk '/crypt/ {print $1}')");
    }

    const auto& check_cryptparts = [&luks_name](const auto& cryptparts, auto functor) {
        for (const auto& cryptpart : cryptparts) {
            if (gucc::utils::exec_checked(fmt::format(FMT_COMPILE("lsblk -lno NAME {} | grep -q '{}'"), cryptpart, luks_name))) {
                functor(cryptpart);
            }
        }
    };

    // Check if LUKS on LVM (parent = lvm /dev/mapper/...)
    auto temp_out = gucc::utils::exec(R"(lsblk -lno NAME,FSTYPE,TYPE,MOUNTPOINT | grep "lvm" | grep "/mnt$" | grep -i "crypto_luks" | uniq | awk '{print "/dev/mapper/"$1}')");
    if (!temp_out.empty()) {
        const auto& cryptparts    = gucc::utils::make_multiline(temp_out);
        const auto& check_functor = [&](const auto& cryptpart) {
            config_data["LUKS_DEV"] = fmt::format(FMT_COMPILE("cryptdevice={}:{}"), cryptpart, luks_name);
            config_data["LVM"]      = 1;
        };
        check_cryptparts(cryptparts, check_functor);
        return;
    }

    // Check if LVM on LUKS
    temp_out = gucc::utils::exec(R"(lsblk -lno NAME,FSTYPE,TYPE | grep " crypt$" | grep -i "LVM2_member" | uniq | awk '{print "/dev/mapper/"$1}')");
    if (!temp_out.empty()) {
        const auto& cryptparts    = gucc::utils::make_multiline(temp_out);
        const auto& check_functor = [&]([[maybe_unused]] const auto& cryptpart) {
            auto& luks_uuid         = std::get<std::string>(config_data["LUKS_UUID"]);
            luks_uuid               = gucc::utils::exec(R"(lsblk -ino NAME,FSTYPE,TYPE,MOUNTPOINT,UUID | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e "/\/mnt /,/part/p" | awk '/crypto_LUKS/ {print $4}')");
            config_data["LUKS_DEV"] = fmt::format(FMT_COMPILE("cryptdevice=UUID={}:{}"), luks_uuid, luks_name);
            config_data["LVM"]      = 1;
        };
        check_cryptparts(cryptparts, check_functor);
        return;
    }

    // Check if LUKS alone (parent = part /dev/...)
    temp_out = gucc::utils::exec(R"(lsblk -lno NAME,FSTYPE,TYPE,MOUNTPOINT | grep "/mnt$" | grep "part" | grep -i "crypto_luks" | uniq | awk '{print "/dev/"$1}')");
    if (!temp_out.empty()) {
        const auto& cryptparts    = gucc::utils::make_multiline(temp_out);
        const auto& check_functor = [&](const auto& cryptpart) {
            auto& luks_uuid         = std::get<std::string>(config_data["LUKS_UUID"]);
            luks_uuid               = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -lno UUID,TYPE,FSTYPE {} | grep 'part' | grep -i 'crypto_luks' | {}"), cryptpart, "awk '{print $1}'"));
            config_data["LUKS_DEV"] = fmt::format(FMT_COMPILE("cryptdevice=UUID={}:{}"), luks_uuid, luks_name);
        };
        check_cryptparts(cryptparts, check_functor);
        return;
    }
}

void recheck_luks() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& root_name = gucc::utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");

    // Check if there is separate encrypted /boot partition
    if (gucc::utils::exec_checked("lsblk | grep '/mnt/boot' | grep -q 'crypt'")) {
        config_data["LUKS"] = 1;
    }
    // Check if root is encrypted and there is no separate /boot
    else if ((gucc::utils::exec_checked("lsblk | grep '/mnt$' | grep -q 'crypt'")) && !gucc::utils::exec_checked("lsblk | grep -q '/mnt/boot$'")) {
        config_data["LUKS"] = 1;
    }
    // Check if root is on encrypted lvm volume
    else if (gucc::utils::exec_checked("lsblk | grep '/mnt/boot' | grep -q 'crypt'") && gucc::utils::exec_checked(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/{}/,/disk/p' | {} | grep -q crypt"), root_name, "awk '{print $6}'"))) {
        config_data["LUKS"] = 1;
    }
}

void get_cryptboot() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    // If /boot is encrypted
    if (!gucc::utils::exec_checked("lsblk | sed -r 's/^[^[:alnum:]]+//' | awk '/\\/mnt\\/boot$/ {print $6}' | grep -q crypt")
        || !gucc::utils::exec_checked(R"(lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/\/mnt\/boot$/,/part/p' | awk '{print $6}' | grep -q crypt)")) {
        return;
    }
    config_data["LUKS"] = 1;

    // Mountpoint is directly on the LUKS device, so LUKS device is the same as root name
    std::string boot_name{gucc::utils::exec("mount | awk '/\\/mnt\\/boot / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g")};
    // Get UUID of the encrypted /boot
    std::string boot_uuid{gucc::utils::exec("lsblk -lno UUID,MOUNTPOINT | awk '/\\mnt\\/boot$/ {print $1}'")};

    // Get the name of the Luks device
    if (!gucc::utils::exec_checked("lsblk -i | grep -q -e 'crypt /mnt'")) {
        // Mountpoint is not directly on LUKS device, so we need to get the crypt device above the mountpoint
        boot_name = gucc::utils::exec(R"(lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/\/mnt\/boot$/,/crypt/p' | awk '/crypt/ {print $1}')");
        boot_uuid = gucc::utils::exec(R"(lsblk -ino NAME,FSTYPE,TYPE,MOUNTPOINT,UUID | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/\/mnt\/boot /,/part/p' | awk '/crypto_LUKS/ {print $4}')");
    }

    // Check if LVM on LUKS
    if (gucc::utils::exec_checked("lsblk -lno TYPE,MOUNTPOINT | grep '/mnt/boot$' | grep -q lvm")) {
        config_data["LVM"] = 1;
    }

    // Add cryptdevice to LUKS_DEV, if not already present (if on same LVM on LUKS as /)
    auto& luks_dev    = std::get<std::string>(config_data["LUKS_DEV"]);
    const auto& found = std::ranges::search(luks_dev, boot_uuid);
    if (found.empty()) {
        luks_dev = fmt::format(FMT_COMPILE("{} cryptdevice=UUID={}:{}"), luks_dev, boot_uuid, boot_name);
    }
}

void boot_encrypted_setting() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    config_data["fde"] = 0;
    auto& fde          = std::get<std::int32_t>(config_data["fde"]);

    // Check if there is separate /boot partition
    if (!gucc::utils::exec_checked("lsblk | grep -q '/mnt/boot$'")) {
        // There is no separate /boot parition
        const auto& root_name = gucc::utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
        const auto& luks      = std::get<std::int32_t>(config_data["LUKS"]);
        // Check if root is encrypted
        if ((luks == 1)
            || gucc::utils::exec_checked(fmt::format(FMT_COMPILE("lsblk '/dev/mapper/{}' | grep -q 'crypt'"), root_name))
            || gucc::utils::exec_checked("lsblk | grep '/mnt$' | grep -q 'crypt'")
            // Check if root is on encrypted lvm volume
            || gucc::utils::exec_checked(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/{}/,/disk/p' | {} | grep -q crypt"), root_name, "awk '{print $6}'"))) {
            fde = 1;
            utils::setup_luks_keyfile();
        }
        return;
    }
    // There is a separate /boot. Check if it is encrypted
    const auto& boot_name = gucc::utils::exec("mount | awk '/\\/mnt\\/boot / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    if (gucc::utils::exec_checked("lsblk | grep '/mnt/boot' | grep -q 'crypt'")
        // Check if the /boot is inside encrypted lvm volume
        || gucc::utils::exec_checked(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/{}/,/disk/p' | {} | grep -q crypt"), boot_name, "awk '{print $6}'"))
        || gucc::utils::exec_checked(fmt::format(FMT_COMPILE("lsblk '/dev/mapper/{}' | grep -q 'crypt'"), boot_name))) {
        fde = 1;
        utils::setup_luks_keyfile();
    }
}

// Ensure that a partition is mounted
bool check_mount() noexcept {
#ifdef NDEVENV
    auto* config_instance       = Config::instance();
    auto& config_data           = config_instance->data();
    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    if (gucc::utils::exec(fmt::format(FMT_COMPILE("findmnt -nl {}"), mountpoint_info)).empty()) {
        return false;
    }
#endif
    return true;
}

// Ensure that CachyOS has been installed
bool check_base() noexcept {
#ifdef NDEVENV
    if (!check_mount()) {
        return false;
    }

    // TODO(vnepogodin): invalidate base install
    // checking just for pacman is not enough
    if (!fs::exists("/mnt/usr/bin/pacman")) {
        tui::detail::msgbox_widget("\nThe CachyOS base must be installed first.\n");
        return false;
    }
#endif
    return true;
}

void id_system() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    // Apple System Detection
    const auto& sys_vendor = gucc::utils::exec("cat /sys/class/dmi/id/sys_vendor");
    if ((sys_vendor == "Apple Inc.") || (sys_vendor == "Apple Computer, Inc.")) {
        gucc::utils::exec("modprobe -r -q efivars || true");  // if MAC
    } else {
        gucc::utils::exec("modprobe -q efivarfs");  // all others
    }

    // BIOS or UEFI Detection
    static constexpr auto efi_path = "/sys/firmware/efi/";
    if (fs::exists(efi_path) && fs::is_directory(efi_path)) {
        // Mount efivarfs if it is not already mounted
        const auto& mount_out = gucc::utils::exec("mount | grep /sys/firmware/efi/efivars");
        if (mount_out.empty()) {
            if (mount("efivarfs", "/sys/firmware/efi/efivars", "efivarfs", 0, "") != 0) {
                perror("utils::id_system");
                exit(1);
            }
        }
        config_data["SYSTEM"] = "UEFI";
    }

    // init system
    const auto& init_sys = gucc::utils::exec("cat /proc/1/comm");
    auto& h_init         = std::get<std::string>(config_data["H_INIT"]);
    if (init_sys == "systemd") {
        h_init = "systemd";
    }

    // TODO(vnepogodin): Test which nw-client is available, including if the service according to $H_INIT is running
    if (h_init == "systemd" && gucc::utils::exec("systemctl is-active NetworkManager") == "active") {
        config_data["NW_CMD"] = "nmtui";
    }
}

void install_cachyos_repo() noexcept {
    const auto& add_arch_specific_repo = [](auto&& isa_level, auto&& repo_name, const auto& isa_levels, [[maybe_unused]] auto&& repos_data) {
        if (!std::ranges::contains(isa_levels, isa_level)) {
            spdlog::warn("{} is not supported", isa_level);
            return;
        }
        spdlog::info("{} is supported", isa_level);

        const auto& repo_list = gucc::detail::pacmanconf::get_repo_list("/etc/pacman.conf");
        if (std::ranges::contains(repo_list, fmt::format(FMT_COMPILE("[{}]"), repo_name))) {
            spdlog::info("'{}' is already added!", repo_name);
            return;
        }

#ifdef NDEVENV
        static constexpr auto pacman_conf             = "/etc/pacman.conf";
        static constexpr auto pacman_conf_cachyos     = "./pacman.conf";
        static constexpr auto pacman_conf_path_backup = "/etc/pacman.conf.bak";
        std::error_code err{};

        if (!fs::copy_file(pacman_conf, pacman_conf_cachyos, err)) {
            spdlog::error("Failed to copy pacman config [{}]", err.message());
            return;
        }

        gucc::detail::pacmanconf::push_repos_front(pacman_conf_cachyos, repos_data);

        spdlog::info("backup old config");
        fs::rename(pacman_conf, pacman_conf_path_backup, err);

        spdlog::info("CachyOS {} Repo changed", repo_name);
        fs::rename(pacman_conf_cachyos, pacman_conf, err);
#endif
    };

    // Check if it's already been applied
    const auto& repo_list = gucc::detail::pacmanconf::get_repo_list("/etc/pacman.conf");
    spdlog::info("install_cachyos_repo: repo_list := '{}'", repo_list);

#ifdef NDEVENV
    gucc::utils::exec("pacman-key --recv-keys F3B607488DB35A47 --keyserver keyserver.ubuntu.com", true);
    gucc::utils::exec("pacman-key --lsign-key F3B607488DB35A47", true);
#endif

    const auto& isa_levels = gucc::cpu::get_isa_levels();

    const auto& is_oracle_vm = gucc::utils::exec_checked("systemd-detect-virt | grep -q oracle");

    static constexpr auto CACHYOS_V1_REPO_STR = R"(
[cachyos]
Include = /etc/pacman.d/cachyos-mirrorlist
)";
    static constexpr auto CACHYOS_V3_REPO_STR = R"(
[cachyos-v3]
Include = /etc/pacman.d/cachyos-v3-mirrorlist
[cachyos-core-v3]
Include = /etc/pacman.d/cachyos-v3-mirrorlist
[cachyos-extra-v3]
Include = /etc/pacman.d/cachyos-v3-mirrorlist
)";
    static constexpr auto CACHYOS_V4_REPO_STR = R"(
[cachyos-v4]
Include = /etc/pacman.d/cachyos-v4-mirrorlist
)";

    add_arch_specific_repo("x86_64", "cachyos", isa_levels, CACHYOS_V1_REPO_STR);
    if (is_oracle_vm) {
        spdlog::info("Oracle VM detected. skipping ISA specific repos");
        return;
    }
    add_arch_specific_repo("x86_64_v3", "cachyos-v3", isa_levels, CACHYOS_V3_REPO_STR);
    add_arch_specific_repo("x86_64_v4", "cachyos-v4", isa_levels, CACHYOS_V4_REPO_STR);
}

bool handle_connection() noexcept {
    bool connected{utils::is_connected()};

#ifdef NDEVENV
    static constexpr std::int32_t CONNECTION_TIMEOUT = 15;
    if (!connected) {
        warning_inter("An active network connection could not be detected, waiting 15 seconds ...\n");

        std::int32_t time_waited{};

        while (!(connected = utils::is_connected())) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            if (time_waited++ >= CONNECTION_TIMEOUT) {
                break;
            }
        }

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
    }

    if (connected) {
        utils::install_cachyos_repo();
        gucc::utils::exec("yes | pacman -Sy --noconfirm", true);
    }
#else
    utils::install_cachyos_repo();
#endif

    return connected;
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

void set_keymap() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    const auto& keymap    = std::get<std::string>(config_data["KEYMAP"]);

    gucc::utils::exec(fmt::format(FMT_COMPILE("loadkeys {}"), keymap));
}

void enable_autologin([[maybe_unused]] const std::string_view& dm, [[maybe_unused]] const std::string_view& username) noexcept {
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    if (!gucc::user::enable_autologin(dm, username, mountpoint)) {
        spdlog::error("Failed to enable autologin");
    }
#endif
}

bool parse_config() noexcept {
    using namespace rapidjson;

    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    spdlog::info("CachyOS installer version '{}'", INSTALLER_VERSION);
    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    spdlog::info("SYSTEM: '{}'", system_info);

    // 1. Open file for reading.
    static constexpr auto file_path = "settings.json";
    std::ifstream ifs{file_path};
    if (!ifs.is_open()) {
        fmt::print(stderr, "Config not found running with defaults\n");
        return true;
    }

    IStreamWrapper isw{ifs};

    // 2. Parse a JSON.
    Document doc;
    doc.ParseStream(isw);

    // Document is a JSON value represents the root of DOM. Root can be either an object or array.
    assert(doc.IsObject());

    assert(doc.HasMember("menus"));
    assert(doc["menus"].IsInt());

    config_data["menus"] = doc["menus"].GetInt();

    auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
    if (doc.HasMember("headless_mode")) {
        assert(doc["headless_mode"].IsBool());
        headless_mode = static_cast<std::int32_t>(doc["headless_mode"].GetBool());
    }

    if (headless_mode) {
        spdlog::info("Running in HEADLESS mode!");
    } else {
        spdlog::info("Running in NORMAL mode!");
    }

    auto& server_mode = std::get<std::int32_t>(config_data["SERVER_MODE"]);
    if (doc.HasMember("server_mode")) {
        assert(doc["server_mode"].IsBool());
        server_mode = static_cast<std::int32_t>(doc["server_mode"].GetBool());
    }

    if (server_mode) {
        spdlog::info("Server profile enabled!");
    }

    if (doc.HasMember("device")) {
        assert(doc["device"].IsString());
        config_data["DEVICE"] = std::string{doc["device"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "'device' field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("fs_name")) {
        assert(doc["fs_name"].IsString());
        config_data["FILESYSTEM_NAME"] = std::string{doc["fs_name"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "'fs_name' field is required in HEADLESS mode!\n");
        return false;
    }
    auto& root_fstype = std::get<std::string>(config_data["FILESYSTEM_NAME"]);

    if (doc.HasMember("partitions")) {
        assert(doc["partitions"].IsArray());

        std::vector<std::string> ready_parts{};
        for (const auto& partition_map : doc["partitions"].GetArray()) {
            assert(partition_map.IsObject());

            const auto& part_obj = partition_map.GetObject();
            assert(partition_map["name"].IsString());
            assert(partition_map["mountpoint"].IsString());
            assert(partition_map["size"].IsString());
            assert(partition_map["type"].IsString());

            // Validate partition type.
            const auto& part_type = std::string{partition_map["type"].GetString()};

            using namespace std::literals;
            static constexpr std::array valid_types{"root"sv, "boot"sv, "additional"sv};
            if (!std::ranges::contains(valid_types, part_type)) {
                fmt::print(stderr, "partition type '{}' is invalid! Valid types: {}.\n", part_type, valid_types);
                return false;
            }
            if (!partition_map.HasMember("fs_name") && (part_type != "root"sv || (root_fstype.empty() && part_type == "root"sv))) {
                fmt::print(stderr, "required field 'fs_name' is missing for partition type '{}'!\n", part_type);
                return false;
            }

            std::string part_fs_name{root_fstype};
            if (partition_map.HasMember("fs_name")) {
                assert(partition_map["fs_name"].IsString());
                part_fs_name = partition_map["fs_name"].GetString();
            }

            // Just to save some space push as single string instead of a new type.
            auto&& part_data = fmt::format(FMT_COMPILE("{}\t{}\t{}\t{}\t{}"), partition_map["name"].GetString(),
                partition_map["mountpoint"].GetString(), partition_map["size"].GetString(), part_fs_name, part_type);
            ready_parts.push_back(std::move(part_data));
        }
        config_data["READY_PARTITIONS"] = std::move(ready_parts);
    } else if (headless_mode) {
        fmt::print(stderr, "'partitions' field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("mount_opts")) {
        assert(doc["mount_opts"].IsString());
        config_data["MOUNT_OPTS"] = std::string{doc["mount_opts"].GetString()};
    }

    if (doc.HasMember("hostname")) {
        assert(doc["hostname"].IsString());
        config_data["HOSTNAME"] = std::string{doc["hostname"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "'hostname' field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("locale")) {
        assert(doc["locale"].IsString());
        config_data["LOCALE"] = std::string{doc["locale"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "'locale' field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("xkbmap")) {
        assert(doc["xkbmap"].IsString());
        config_data["XKBMAP"] = std::string{doc["xkbmap"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "'xkbmap' field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("timezone")) {
        assert(doc["timezone"].IsString());
        config_data["TIMEZONE"] = std::string{doc["timezone"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "'timezone' field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("user_name") && doc.HasMember("user_pass") && doc.HasMember("user_shell")) {
        assert(doc["user_name"].IsString());
        assert(doc["user_pass"].IsString());
        assert(doc["user_shell"].IsString());
        config_data["USER_NAME"]  = std::string{doc["user_name"].GetString()};
        config_data["USER_PASS"]  = std::string{doc["user_pass"].GetString()};
        config_data["USER_SHELL"] = std::string{doc["user_shell"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "'user_name', 'user_pass', 'user_shell' fields are required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("root_pass")) {
        assert(doc["root_pass"].IsString());
        config_data["ROOT_PASS"] = std::string{doc["root_pass"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "'root_pass' field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("kernel")) {
        assert(doc["kernel"].IsString());
        config_data["KERNEL"] = std::string{doc["kernel"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "'kernel' field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("desktop")) {
        assert(doc["desktop"].IsString());
        config_data["DE"] = std::string{doc["desktop"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "'desktop' field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("bootloader")) {
        assert(doc["bootloader"].IsString());
        config_data["BOOTLOADER"] = std::string{doc["bootloader"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "'bootloader' field is required in HEADLESS mode!\n");
        return false;
    }

    std::string drivers_type{"free"};
    if (doc.HasMember("drivers_type")) {
        assert(doc["drivers_type"].IsString());
        drivers_type = doc["drivers_type"].GetString();

        if (drivers_type != "nonfree" && drivers_type != "free") {
            spdlog::error("Unknown value: {}!", drivers_type);
            drivers_type = "free";
        }
    }

    config_data["DRIVERS_TYPE"] = drivers_type;

    if (doc.HasMember("post_install")) {
        assert(doc["post_install"].IsString());
        config_data["POST_INSTALL"] = std::string{doc["post_install"].GetString()};
    }
    return true;
}

void setup_luks_keyfile() noexcept {
    // Add keyfile to luks
    const auto& root_name          = gucc::utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    const auto& root_part          = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/{}/,/part/p' | {} | tr -cd '[:alnum:]'"), root_name, "awk '/part/ {print $1}'"));
    const auto& partition          = fmt::format(FMT_COMPILE("/dev/{}"), root_part);
    const auto& number_of_lukskeys = utils::to_int(gucc::utils::exec(fmt::format(FMT_COMPILE("cryptsetup luksDump '{}' | grep 'ENABLED' | wc -l"), partition)));
    if (number_of_lukskeys < 4) {
        // Create a keyfile
#ifdef NDEVENV
        const std::string_view keyfile_path{"/mnt/crypto_keyfile.bin"};
        if (!gucc::crypto::luks1_setup_keyfile(keyfile_path, "/mnt", partition, "--pbkdf-force-iterations 200000")) {
            return;
        }
#endif
    }
}

void grub_mkconfig() noexcept {
    utils::arch_chroot("grub-mkconfig -o /boot/grub/grub.cfg");
    // check_for_error "grub-mkconfig" $?
}

void set_lightdm_greeter() {
    auto* config_instance      = Config::instance();
    auto& config_data          = config_instance->data();
    const auto& mountpoint     = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& xgreeters_path = fmt::format(FMT_COMPILE("{}/usr/share/xgreeters"), mountpoint);
    /* clang-format off */
    if (!fs::exists(xgreeters_path)) { return; }
    for (const auto& name : fs::directory_iterator{xgreeters_path}) {
        const auto& temp = name.path().filename().string();
        if (!temp.starts_with("lightdm-") && !temp.ends_with("-greeter")) { continue; }
        /* clang-format on */
        if (temp == "lightdm-gtk-greeter") {
            continue;
        }
        gucc::utils::exec(fmt::format(FMT_COMPILE("sed -i -e 's/^.*greeter-session=.*/greeter-session={}/' {}/etc/lightdm/lightdm.conf"), temp, mountpoint));
        break;
    }
}

void enable_services() noexcept {
    spdlog::info("Enabling services...");

#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& zfs        = std::get<std::int32_t>(config_data["ZFS"]);

    static constexpr std::array enable_systemd{"avahi-daemon", "bluetooth", "cronie", "ModemManager", "NetworkManager", "org.cups.cupsd", "tlp", "haveged", "ufw", "apparmor", "fstrim.timer"};
    for (auto&& service_name : enable_systemd) {
        if (!fs::exists(fmt::format(FMT_COMPILE("{}/usr/lib/systemd/system/{}.service"), mountpoint, service_name))) {
            continue;
        }
        gucc::services::enable_systemd_service(service_name, mountpoint);
        spdlog::info("enabled service {}", service_name);
    }

    utils::arch_chroot("systemctl disable pacman-init", false);
    spdlog::info("enabled service pacman-init");

    // enable display manager for systemd
    const auto& temp = fmt::format(FMT_COMPILE("arch-chroot {} pacman -Qq"), mountpoint);
    if (gucc::utils::exec_checked(fmt::format(FMT_COMPILE("{} lightdm &> /dev/null"), temp))) {
        utils::set_lightdm_greeter();
        gucc::services::enable_systemd_service("lightdm"sv, mountpoint);
    } else if (gucc::utils::exec_checked(fmt::format(FMT_COMPILE("{} sddm &> /dev/null"), temp))) {
        gucc::services::enable_systemd_service("sddm"sv, mountpoint);
    } else if (gucc::utils::exec_checked(fmt::format(FMT_COMPILE("{} gdm &> /dev/null"), temp))) {
        gucc::services::enable_systemd_service("gdm"sv, mountpoint);
    } else if (gucc::utils::exec_checked(fmt::format(FMT_COMPILE("{} lxdm &> /dev/null"), temp))) {
        gucc::services::enable_systemd_service("lxdm"sv, mountpoint);
    } else if (gucc::utils::exec_checked(fmt::format(FMT_COMPILE("{} ly &> /dev/null"), temp))) {
        gucc::services::enable_systemd_service("ly"sv, mountpoint);
    }

    /* clang-format off */
    if (zfs == 0) { return; }
    /* clang-format on */

    // if we are using a zfs we should enable the zfs services
    for (auto&& service_name : {"zfs.target"sv, "zfs-import-cache"sv, "zfs-mount"sv, "zfs-import.target"sv}) {
        gucc::services::enable_systemd_service(service_name, mountpoint);
    }

    // we also need create the cachefile
    gucc::utils::exec(fmt::format(FMT_COMPILE("zpool set cachefile=/etc/zfs/zpool.cache $(findmnt {} -lno SOURCE | {}) 2>>/tmp/cachyos-install.log"), mountpoint, "awk -F / '{print $1}'"), true);
    gucc::utils::exec(fmt::format(FMT_COMPILE("cp /etc/zfs/zpool.cache {}/etc/zfs/zpool.cache 2>>/tmp/cachyos-install.log"), mountpoint), true);
#endif
}

void final_check() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    config_data["CHECKLIST"] = "";
    auto& checklist          = std::get<std::string>(config_data["CHECKLIST"]);

    // Check if base is installed
    if (!fs::exists("/mnt/.base_installed")) {
        checklist += "- Base is not installed\n";
        return;
    }

    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    const auto& mountpoint  = std::get<std::string>(config_data["MOUNTPOINT"]);
    // Check if bootloader is installed
    if (system_info == "BIOS") {
        if (!gucc::utils::exec_checked(fmt::format(FMT_COMPILE("arch-chroot {} pacman -Qq grub &> /dev/null"), mountpoint))) {
            checklist += "- Bootloader is not installed\n";
        }
    }

    // Check if fstab is generated
    if (!gucc::utils::exec_checked(fmt::format(FMT_COMPILE("grep -qv '^#' {}/etc/fstab 2>/dev/null"), mountpoint))) {
        checklist += "- Fstab has not been generated\n";
    }

    // Check if video-driver has been installed
    //[[ ! -e /mnt/.video_installed ]] && echo "- $_GCCheck" >> ${CHECKLIST}

    // Check if locales have been generated
    if (utils::to_int(gucc::utils::exec(fmt::format(FMT_COMPILE("arch-chroot {} locale -a | wc -l"), mountpoint), false)) < 3) {
        checklist += "- Locales have not been generated\n";
    }

    // Check if root password has been set
    if (gucc::utils::exec_checked(fmt::format(FMT_COMPILE("arch-chroot {} passwd --status root | cut -d' ' -f2 | grep -q 'NP'"), mountpoint))) {
        checklist += "- Root password is not set\n";
    }

    // check if user account has been generated
    if (!fs::exists("/mnt/home")) {
        checklist += "- No user accounts have been generated\n";
    }
}

}  // namespace utils
