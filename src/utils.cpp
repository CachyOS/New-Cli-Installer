#include "utils.hpp"
#include "config.hpp"
#include "definitions.hpp"
#include "follow_process_log.hpp"
#include "subprocess.h"
#include "tui.hpp"
#include "widgets.hpp"

#include <algorithm>      // for transform
#include <array>          // for array
#include <bit>            // for bit_cast
#include <chrono>         // for filesystem, seconds
#include <cstdint>        // for int32_t
#include <cstdio>         // for feof, fgets, pclose, perror, popen
#include <cstdlib>        // for exit, WIFEXITED, WIFSIGNALED
#include <filesystem>     // for exists, is_directory
#include <fstream>        // for ofstream
#include <iostream>       // for basic_istream, cin
#include <string>         // for operator==, string, basic_string, allocator
#include <sys/mount.h>    // for mount
#include <sys/wait.h>     // for waitpid
#include <thread>         // for sleep_for
#include <unistd.h>       // for execvp, fork
#include <unordered_map>  // for unordered_map

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <range/v3/algorithm/for_each.hpp>
#include <range/v3/algorithm/reverse.hpp>
#include <range/v3/algorithm/search.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/split.hpp>
#include <range/v3/view/transform.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef NDEVENV
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

namespace utils {

bool is_connected() noexcept {
#ifdef NDEVENV
    /* clang-format off */
    auto r = cpr::Get(cpr::Url{"https://cachyos.org"},
             cpr::Timeout{1000});
    /* clang-format on */
    return cpr::status::is_success(static_cast<std::int32_t>(r.status_code)) || cpr::status::is_redirect(static_cast<std::int32_t>(r.status_code));
#else
    return true;
#endif
}

bool check_root() noexcept {
#ifdef NDEVENV
    return (utils::exec("whoami") == "root");
#else
    return true;
#endif
}

void clear_screen() noexcept {
    static constexpr auto CLEAR_SCREEN_ANSI = "\033[1;1H\033[2J";
    output_inter("{}", CLEAR_SCREEN_ANSI);
}

void arch_chroot(const std::string_view& command, bool follow) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& mountpoint    = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& cmd_formatted = fmt::format(FMT_COMPILE("arch-chroot {} {} 2>>/tmp/cachyos-install.log 2>&1"), mountpoint, command);
    if (follow) {
        tui::detail::follow_process_log_widget({"/bin/sh", "-c", cmd_formatted});
        return;
    }
    utils::exec(cmd_formatted);
}

void exec_follow(const std::vector<std::string>& vec, std::string& process_log, bool& running, subprocess_s& child, bool async) noexcept {
    if (!async) {
        spdlog::error("Implement me!");
        return;
    }

    std::vector<char*> args;
    std::transform(vec.cbegin(), vec.cend(), std::back_inserter(args),
        [=](const std::string& arg) -> char* { return std::bit_cast<char*>(arg.data()); });
    args.push_back(nullptr);

    static std::array<char, 1048576 + 1> data{};
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

    uint32_t index{};
    uint32_t bytes_read;
    do {
        bytes_read = subprocess_read_stdout(&process, data.data() + index,
            static_cast<uint32_t>(data.size()) - 1 - index);

        index += bytes_read;
        process_log = data.data();
    } while (bytes_read != 0);

    subprocess_join(&process, &ret);
    subprocess_destroy(&process);
    child   = process;
    running = false;
}

void exec(const std::vector<std::string>& vec) noexcept {
    std::int32_t status{};
    auto pid = fork();
    if (pid == 0) {
        std::vector<char*> args;
        std::transform(vec.cbegin(), vec.cend(), std::back_inserter(args),
            [=](const std::string& arg) -> char* { return const_cast<char*>(arg.data()); });
        args.push_back(nullptr);

        char** command = args.data();
        execvp(command[0], command);
    } else {
        do {
            waitpid(pid, &status, 0);
        } while ((!WIFEXITED(status)) && (!WIFSIGNALED(status)));
    }
}

// https://github.com/sheredom/subprocess.h
// https://gist.github.com/konstantint/d49ab683b978b3d74172
// https://github.com/arun11299/cpp-subprocess/blob/master/subprocess.hpp#L1218
// https://stackoverflow.com/questions/11342868/c-interface-for-interactive-bash
// https://github.com/hniksic/rust-subprocess
std::string exec(const std::string_view& command, const bool& interactive) noexcept {
    if (interactive) {
        const auto& ret_code = system(command.data());
        return std::to_string(ret_code);
    }

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.data(), "r"), pclose);

    if (!pipe) {
        spdlog::error("popen failed! '{}'", command);
        return "-1";
    }

    std::string result{};
    std::array<char, 128> buffer{};
    while (!feof(pipe.get())) {
        if (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
    }

    if (result.ends_with('\n')) {
        result.pop_back();
    }

    return result;
}

bool prompt_char(const char* prompt, const char* color, char* read) noexcept {
    fmt::print("{}{}{}\n", color, prompt, RESET);

    std::string tmp{};
    if (std::getline(std::cin, tmp)) {
        const auto& read_char = (tmp.length() == 1) ? tmp[0] : '\0';
        if (read != nullptr)
            *read = read_char;

        return true;
    }

    return false;
}

auto make_multiline(const std::string_view& str, bool reverse, const std::string_view&& delim) noexcept -> std::vector<std::string> {
    static constexpr auto functor = [](auto&& rng) {
        return std::string_view(&*rng.begin(), static_cast<size_t>(ranges::distance(rng)));
    };
    static constexpr auto second = [](auto&& rng) { return rng != ""; };

#if defined(__clang__)
    const auto& splitted_view = str
        | ranges::views::split(delim);
    const auto& view_res = splitted_view
        | ranges::views::transform(functor);
#else
    const auto& view_res = str
        | ranges::views::split(delim)
        | ranges::views::transform(functor);
#endif

    std::vector<std::string> lines{};
    ranges::for_each(view_res | ranges::views::filter(second), [&](auto&& rng) { lines.emplace_back(rng); });
    if (reverse) {
        ranges::reverse(lines);
    }
    return lines;
}

auto make_multiline(std::vector<std::string>& multiline, bool reverse, const std::string_view&& delim) noexcept -> std::string {
    std::string res{};
    for (const auto& line : multiline) {
        res += line;
        res += delim.data();
    }

    if (reverse) {
        ranges::reverse(res);
    }

    return res;
}

// install a pkg in the live session if not installed
void inst_needed(const std::string_view& pkg) noexcept {
    if (utils::exec(fmt::format(FMT_COMPILE("pacman -Qq {} &>/dev/null"), pkg), true) != "0") {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        utils::clear_screen();
        tui::detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("pacman -Sy --noconfirm {}"), pkg)});
        // utils::exec(fmt::format(FMT_COMPILE("pacman -Sy --noconfirm {}"), pkg));
    }
}

// Unmount partitions.
void umount_partitions() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    auto mount_info             = utils::exec(fmt::format(FMT_COMPILE("mount | grep \"{}\" | {}"), mountpoint_info, "awk '{print $3}' | sort -r"));
#ifdef NDEVENV
    utils::exec("swapoff -a");
#endif

    const auto& lines = utils::make_multiline(mount_info);
    for (const auto& line : lines) {
#ifdef NDEVENV
        umount(line.c_str());
#else
        spdlog::debug("{}\n", line);
#endif
    }
}

// Securely destroy all data on a given device.
void secure_wipe() noexcept {
    auto* config_instance   = Config::instance();
    auto& config_data       = config_instance->data();
    const auto& device_info = std::get<std::string>(config_data["DEVICE"]);

#ifdef NDEVENV
    utils::inst_needed("wipe");
    tui::detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("wipe -Ifre {}"), device_info)});
    // utils::exec(fmt::format(FMT_COMPILE("wipe -Ifre {}"), device_info));
#else
    spdlog::debug("{}\n", device_info);
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
    static constexpr auto other_piece = "sed 's/part$/\\/dev\\//g' | sed 's/lvm$\\|crypt$/\\/dev\\/mapper\\//g' | awk '{print $3$1 \" \" $2}' | awk '!/mapper/{a[++i]=$0;next}1;END{while(x<length(a))print a[++x]}' ; zfs list -Ht volume -o name,volsize 2>/dev/null | awk '{printf \"/dev/zvol/%s %s\\n\", $1, $2}'";
    const auto& partitions_tmp        = utils::exec(fmt::format(FMT_COMPILE("lsblk -lno NAME,SIZE,TYPE | grep '{}' | {}"), include_part, other_piece));

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

    const auto& partition_list = utils::make_multiline(partitions_tmp, true);
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

// List partitions to be hidden from the mounting menu
std::string list_mounted() noexcept {
    utils::exec("lsblk -l | awk '$7 ~ /mnt/ {print $1}' > /tmp/.mounted");
    return utils::exec("echo /dev/* /dev/mapper/* | xargs -n1 2>/dev/null | grep -f /tmp/.mounted");
}

std::string list_containing_crypt() noexcept {
    return utils::exec("blkid | awk '/TYPE=\"crypto_LUKS\"/{print $1}' | sed 's/.$//'");
}

std::string list_non_crypt() noexcept {
    return utils::exec("blkid | awk '!/TYPE=\"crypto_LUKS\"/{print $1}' | sed 's/.$//'");
}

void get_cryptroot() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    // Identify if /mnt or partition is type "crypt" (LUKS on LVM, or LUKS alone)
    if ((utils::exec("lsblk | sed -r 's/^[^[:alnum:]]+//' | awk '/\\/mnt$/ {print $6}' | grep -q crypt", true) != "0")
        || (utils::exec("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/\\/mnt$/,/part/p\" | awk '{print $6}' | grep -q crypt", true) != "0")) {
        return;
    }

    config_data["LUKS"] = 1;
    auto& luks_name     = std::get<std::string>(config_data["LUKS_ROOT_NAME"]);
    luks_name           = utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    // Get the name of the Luks device
    if (utils::exec("lsblk -i | grep -q -e \"crypt /mnt\"", true) != "0") {
        // Mountpoint is not directly on LUKS device, so we need to get the crypt device above the mountpoint
        luks_name = utils::exec("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/\\/mnt$/,/crypt/p\" | awk '/crypt/ {print $1}'");
    }

    const auto& check_cryptparts = [&luks_name](const auto& cryptparts, auto functor) {
        for (const auto& cryptpart : cryptparts) {
            if (!utils::exec(fmt::format(FMT_COMPILE("lsblk -lno NAME {} | grep \"{}\""), cryptpart, luks_name)).empty()) {
                functor(cryptpart);
            }
        }
    };

    // Check if LUKS on LVM (parent = lvm /dev/mapper/...)
    auto temp_out = utils::exec("lsblk -lno NAME,FSTYPE,TYPE,MOUNTPOINT | grep \"lvm\" | grep \"/mnt$\" | grep -i \"crypto_luks\" | uniq | awk '{print \"/dev/mapper/\"$1}'");
    if (!temp_out.empty()) {
        const auto& cryptparts    = utils::make_multiline(temp_out);
        const auto& check_functor = [&](const auto& cryptpart) {
            config_data["LUKS_DEV"] = fmt::format(FMT_COMPILE("cryptdevice={}:{}"), cryptpart, luks_name);
            config_data["LVM"]      = 1;
        };
        check_cryptparts(cryptparts, check_functor);
        return;
    }

    // Check if LVM on LUKS
    temp_out = utils::exec("lsblk -lno NAME,FSTYPE,TYPE | grep \" crypt$\" | grep -i \"LVM2_member\" | uniq | awk '{print \"/dev/mapper/\"$1}'");
    if (!temp_out.empty()) {
        const auto& cryptparts    = utils::make_multiline(temp_out);
        const auto& check_functor = [&]([[maybe_unused]] const auto cryptpart) {
            auto& luks_uuid         = std::get<std::string>(config_data["LUKS_UUID"]);
            luks_uuid               = utils::exec("lsblk -ino NAME,FSTYPE,TYPE,MOUNTPOINT,UUID | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/\\/mnt /,/part/p\" | awk '/crypto_LUKS/ {print $4}'");
            config_data["LUKS_DEV"] = fmt::format(FMT_COMPILE("cryptdevice=UUID={}:{}"), luks_uuid, luks_name);
            config_data["LVM"]      = 1;
        };
        check_cryptparts(cryptparts, check_functor);
        return;
    }

    // Check if LUKS alone (parent = part /dev/...)
    temp_out = utils::exec("lsblk -lno NAME,FSTYPE,TYPE,MOUNTPOINT | grep \"/mnt$\" | grep \"part\" | grep -i \"crypto_luks\" | uniq | awk '{print \"/dev/\"$1}'");
    if (!temp_out.empty()) {
        const auto& cryptparts    = utils::make_multiline(temp_out);
        const auto& check_functor = [&](const auto cryptpart) {
            auto& luks_uuid         = std::get<std::string>(config_data["LUKS_UUID"]);
            luks_uuid               = utils::exec(fmt::format(FMT_COMPILE("lsblk -lno UUID,TYPE,FSTYPE {} | grep \"part\" | grep -i \"crypto_luks\" | {}"), cryptpart, "awk '{print $1}'"));
            config_data["LUKS_DEV"] = fmt::format(FMT_COMPILE("cryptdevice=UUID={}:{}"), luks_uuid, luks_name);
        };
        check_cryptparts(cryptparts, check_functor);
        return;
    }
}

void recheck_luks() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& root_name = utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");

    // Check if there is separate encrypted /boot partition
    if (utils::exec("lsblk | grep '/mnt/boot' | grep -q 'crypt'", true) == "0") {
        config_data["LUKS"] = 1;
    }
    // Check if root is encrypted and there is no separate /boot
    else if ((utils::exec("lsblk | grep \"/mnt$\" | grep -q 'crypt'", true) == "0") && utils::exec("lsblk | grep \"/mnt/boot$\"", false).empty()) {
        config_data["LUKS"] = 1;
    }
    // Check if root is on encrypted lvm volume
    else if ((utils::exec("lsblk | grep '/mnt/boot' | grep -q 'crypt'", true) == "0") && (utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/disk/p\" | {} | grep -q crypt"), root_name, "awk '{print $6}'"), true) == "0")) {
        config_data["LUKS"] = 1;
    }
}

void get_cryptboot() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    // If /boot is encrypted
    if ((utils::exec("lsblk | sed -r 's/^[^[:alnum:]]+//' | awk '/\\/mnt\\/boot$/ {print $6}' | grep -q crypt", true) != "0")
        || (utils::exec("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/\\/mnt\\/boot$/,/part/p\" | awk '{print $6}' | grep -q crypt", true) != "0")) {
        return;
    }
    config_data["LUKS"] = 1;

    // Mountpoint is directly on the LUKS device, so LUKS device is the same as root name
    std::string boot_name{utils::exec("mount | awk '/\\/mnt\\/boot / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g")};
    // Get UUID of the encrypted /boot
    std::string boot_uuid{utils::exec("lsblk -lno UUID,MOUNTPOINT | awk '/\\mnt\\/boot$/ {print $1}'")};

    // Get the name of the Luks device
    if (utils::exec("lsblk -i | grep -q -e \"crypt /mnt\"", true) != "0") {
        // Mountpoint is not directly on LUKS device, so we need to get the crypt device above the mountpoint
        boot_name = utils::exec("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/\\/mnt\\/boot$/,/crypt/p\" | awk '/crypt/ {print $1}'");
        boot_uuid = utils::exec("lsblk -ino NAME,FSTYPE,TYPE,MOUNTPOINT,UUID | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/\\/mnt\\/boot /,/part/p\" | awk '/crypto_LUKS/ {print $4}'");
    }

    // Check if LVM on LUKS
    if (utils::exec("lsblk -lno TYPE,MOUNTPOINT | grep \"/mnt/boot$\" | grep -q lvm", true) == "0") {
        config_data["LVM"] = 1;
    }

    // Add cryptdevice to LUKS_DEV, if not already present (if on same LVM on LUKS as /)
    auto& luks_dev    = std::get<std::string>(config_data["LUKS_DEV"]);
    const auto& found = ranges::search(luks_dev, boot_uuid);
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
    if (utils::exec("lsblk | grep \"/mnt/boot$\"").empty()) {
        // There is no separate /boot parition
        const auto& root_name = utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
        const auto& luks      = std::get<std::int32_t>(config_data["LUKS"]);
        // Check if root is encrypted
        if ((luks == 1)
            || (utils::exec(fmt::format(FMT_COMPILE("lsblk \"/dev/mapper/{}\" | grep -q 'crypt'"), root_name), true) == "0")
            || (utils::exec("lsblk | grep \"/mnt$\" | grep -q 'crypt'", true) == "0")
            // Check if root is on encrypted lvm volume
            || (utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/disk/p\" | {} | grep -q crypt"), root_name, "awk '{print $6}'"), true) == "0")) {
            fde = 1;
            utils::setup_luks_keyfile();
        }
        return;
    }
    // There is a separate /boot. Check if it is encrypted
    const auto& boot_name = utils::exec("mount | awk '/\\/mnt\\/boot / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    if ((utils::exec("lsblk | grep '/mnt/boot' | grep -q 'crypt'", true) == "0")
        // Check if the /boot is inside encrypted lvm volume
        || (utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/disk/p\" | {} | grep -q crypt"), boot_name, "awk '{print $6}'"), true) == "0")
        || (utils::exec(fmt::format(FMT_COMPILE("lsblk \"/dev/mapper/{}\" | grep -q 'crypt'"), boot_name), true) == "0")) {
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
    if (utils::exec(fmt::format(FMT_COMPILE("findmnt -nl {}"), mountpoint_info)) == "") {
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
    const auto& sys_vendor = utils::exec("cat /sys/class/dmi/id/sys_vendor");
    if ((sys_vendor == "Apple Inc.") || (sys_vendor == "Apple Computer, Inc."))
        utils::exec("modprobe -r -q efivars || true");  // if MAC
    else
        utils::exec("modprobe -q efivarfs");  // all others

    // BIOS or UEFI Detection
    static constexpr auto efi_path = "/sys/firmware/efi/";
    if (fs::exists(efi_path) && fs::is_directory(efi_path)) {
        // Mount efivarfs if it is not already mounted
        const auto& mount_out = utils::exec("mount | grep /sys/firmware/efi/efivars");
        if (mount_out.empty()) {
            if (mount("efivarfs", "/sys/firmware/efi/efivars", "efivarfs", 0, "") != 0) {
                perror("utils::id_system");
                exit(1);
            }
        }
        config_data["SYSTEM"] = "UEFI";
    }

    // init system
    const auto& init_sys = utils::exec("cat /proc/1/comm");
    auto& h_init         = std::get<std::string>(config_data["H_INIT"]);
    if (init_sys == "systemd")
        h_init = "systemd";

    // TODO: Test which nw-client is available, including if the service according to $H_INIT is running
    if (h_init == "systemd" && utils::exec("systemctl is-active NetworkManager") == "active")
        config_data["NW_CMD"] = "nmtui";
}

void try_v3() noexcept {
    const auto& ret_status = utils::exec("/lib/ld-linux-x86-64.so.2 --help | grep \"x86-64-v3 (supported, searched)\" &> /dev/null", true);
    if (ret_status != "0") {
        spdlog::warn("x86-64-v3 is not supported");
        return;
    }
    spdlog::info("x86-64-v3 is supported");

#ifdef NDEVENV
    static constexpr auto pacman_conf             = "/etc/pacman.conf";
    static constexpr auto pacman_conf_cachyos     = "/etc/pacman-more.conf";
    static constexpr auto pacman_conf_path_backup = "/etc/pacman.conf.bak";
    std::error_code err{};

    // Check if it's already been applied
    if (utils::exec("cat /etc/pacman.conf | grep \"\\-v3\\|_v3\" &> /dev/null", true) == "0") {
        return;
    }

    utils::exec(fmt::format(FMT_COMPILE("sed -i 's/Architecture = auto/#Architecture = auto/' {}"), pacman_conf_cachyos));
    utils::exec(fmt::format(FMT_COMPILE("sed -i 's/#<disabled_v3>//g' {}"), pacman_conf_cachyos));

    spdlog::info("backup old config");
    fs::rename(pacman_conf, pacman_conf_path_backup, err);

    spdlog::info("CachyOS -v3 Repo changed");
    fs::rename(pacman_conf_cachyos, pacman_conf, err);
#endif
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
        utils::try_v3();
        utils::exec("yes | pacman -Sy --noconfirm", true);
    }
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
        utils::exec("iwctl", true);
        break;
    }
}

void set_keymap() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    const auto& keymap    = std::get<std::string>(config_data["KEYMAP"]);

    utils::exec(fmt::format(FMT_COMPILE("loadkeys {}"), keymap));
}

void enable_autologin([[maybe_unused]] const std::string_view& dm, [[maybe_unused]] const std::string_view& user) noexcept {
#ifdef NDEVENV
    // enable autologin
    if (dm == "gdm") {
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^AutomaticLogin=*/AutomaticLogin={}/g' /mnt/etc/gdm/custom.conf"), user));
        utils::exec("sed -i 's/^AutomaticLoginEnable=*/AutomaticLoginEnable=true/g' /mnt/etc/gdm/custom.conf");
        utils::exec("sed -i 's/^TimedLoginEnable=*/TimedLoginEnable=true/g' /mnt/etc/gdm/custom.conf");
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^TimedLogin=*/TimedLoginEnable={}/g' /mnt/etc/gdm/custom.conf"), user));
        utils::exec("sed -i 's/^TimedLoginDelay=*/TimedLoginDelay=0/g' /mnt/etc/gdm/custom.conf");
    } else if (dm == "lightdm") {
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^#autologin-user=/autologin-user={}/' /mnt/etc/lightdm/lightdm.conf"), user));
        utils::exec("sed -i 's/^#autologin-user-timeout=0/autologin-user-timeout=0/' /mnt/etc/lightdm/lightdm.conf");

        utils::arch_chroot("groupadd -r autologin", false);
        utils::arch_chroot(fmt::format(FMT_COMPILE("gpasswd -a {} autologin"), user), false);
    } else if (dm == "sddm") {
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^User=/User={}/g' /mnt/etc/sddm.conf"), user));
    } else if (dm == "lxdm") {
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^# autologin=dgod/autologin={}/g' /mnt/etc/lxdm/lxdm.conf"), user));
    }
#endif
}

void set_schedulers() noexcept {
#ifdef NDEVENV
    static constexpr auto rules_path = "/mnt/etc/udev/rules.d/60-ioscheduler.rules";
    if (!fs::exists(rules_path)) {
        static constexpr auto ioscheduler_rules = R"(# set scheduler for NVMe
ACTION=="add|change", KERNEL=="nvme[0-9]n[0-9]", ATTR{queue/scheduler}="none"
# set scheduler for SSD and eMMC
ACTION=="add|change", KERNEL=="sd[a-z]|mmcblk[0-9]*", ATTR{queue/rotational}=="0", ATTR{queue/scheduler}="mq-deadline"
# set scheduler for rotating disks
ACTION=="add|change", KERNEL=="sd[a-z]", ATTR{queue/rotational}=="1", ATTR{queue/scheduler}="bfq")";
        std::ofstream rules{rules_path};
        rules << ioscheduler_rules;
    }
    utils::exec("vim /mnt/etc/udev/rules.d/60-ioscheduler.rules", true);
#else
    utils::exec("vim /etc/udev/rules.d/60-ioscheduler.rules", true);
#endif
}

void set_swappiness() noexcept {
#ifdef NDEVENV
    static constexpr auto rules_path = "/mnt/etc/sysctl.d/99-sysctl.conf";
    if (!fs::exists(rules_path)) {
        static constexpr auto sysctl_rules = R"(vm.swappiness = 10
vm.vfs_cache_pressure = 50
#vm.dirty_ratio = 3)";
        std::ofstream rules{rules_path};
        rules << sysctl_rules;
    }
    utils::exec("vim /mnt/etc/sysctl.d/99-sysctl.conf", true);
#else
    utils::exec("vim /etc/sysctl.d/99-sysctl.conf", true);
#endif
}

void parse_config() noexcept {
    /*
    using namespace simdjson;

    ondemand::parser parser;
    auto json = padded_string::load("test.json");
    auto doc  = parser.iterate(json);

    for (auto entry : doc["steps"]) {
        std::string_view step;
        entry.get(step);
        // spdlog::debug(step);
    }
    */
}

void setup_luks_keyfile() noexcept {
    // Add keyfile to luks
    const auto& root_name          = utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    const auto& root_part          = utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/part/p\" | {} | tr -cd '[:alnum:]'"), root_name, "awk '/part/ {print $1}'"));
    const auto& number_of_lukskeys = to_int(utils::exec(fmt::format(FMT_COMPILE("cryptsetup luksDump /dev/\"{}\" | grep \"ENABLED\" | wc -l"), root_part)));
    if (number_of_lukskeys < 4) {
        // Create a keyfile
#ifdef NDEVENV
        if (!fs::exists("/mnt/crypto_keyfile.bin")) {
            const auto& ret_status = utils::exec("dd bs=512 count=4 if=/dev/urandom of=/mnt/crypto_keyfile.bin", true);
            /* clang-format off */
            if (ret_status == "0") { spdlog::info("Generating a keyfile"); }
            /* clang-format on */
        }
        utils::exec("chmod 000 /mnt/crypto_keyfile.bin");
        spdlog::info("Adding the keyfile to the LUKS configuration");
        auto ret_status = utils::exec(fmt::format(FMT_COMPILE("cryptsetup --pbkdf-force-iterations 200000 luksAddKey /dev/\"{}\" /mnt/crypto_keyfile.bin"), root_part), true);
        /* clang-format off */
        if (ret_status != "0") { spdlog::info("Something went wrong with adding the LUKS key. Is /dev/{} the right partition?", root_part); }
        /* clang-format on */

        // Add keyfile to initcpio
        ret_status = utils::exec("grep -q '/crypto_keyfile.bin' /mnt/etc/mkinitcpio.conf || sed -i '/FILES/ s~)~/crypto_keyfile.bin)~' /mnt/etc/mkinitcpio.conf", true);
        /* clang-format off */
        if (ret_status == "0") { spdlog::info("Adding keyfile to the initcpio"); }
        /* clang-format on */
        utils::arch_chroot("mkinitcpio -P");
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
        if (!temp.starts_with("lightdm-") && !temp.starts_with("-greeter")) { continue; }
        /* clang-format on */
        if (temp == "lightdm-gtk-greeter") {
            continue;
        }
        utils::exec(fmt::format(FMT_COMPILE("sed -i -e \"s/^.*greeter-session=.*/greeter-session={}/\" {}/etc/lightdm/lightdm.conf"), temp, mountpoint));
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
    for (auto&& service : enable_systemd) {
        if (!fs::exists(fmt::format(FMT_COMPILE("{}/usr/lib/systemd/system/{}.service"), mountpoint, service))) {
            continue;
        }
        utils::arch_chroot(fmt::format(FMT_COMPILE("systemctl enable {}"), service), false);
        spdlog::info("enabled service {}", service);
    }

    utils::arch_chroot("systemctl disable pacman-init", false);
    spdlog::info("enabled service pacman-init");

    // enable display manager for systemd
    const auto& temp = fmt::format(FMT_COMPILE("arch-chroot {} pacman -Qq"), mountpoint);
    if (utils::exec(fmt::format(FMT_COMPILE("{} lightdm &> /dev/null"), temp), true) == "0") {
        utils::set_lightdm_greeter();
        utils::arch_chroot("systemctl enable lightdm", false);
    } else if (utils::exec(fmt::format(FMT_COMPILE("{} sddm &> /dev/null"), temp), true) == "0") {
        utils::arch_chroot("systemctl enable sddm", false);
    } else if (utils::exec(fmt::format(FMT_COMPILE("{} gdm &> /dev/null"), temp), true) == "0") {
        utils::arch_chroot("systemctl enable gdm", false);
    } else if (utils::exec(fmt::format(FMT_COMPILE("{} lxdm &> /dev/null"), temp), true) == "0") {
        utils::arch_chroot("systemctl enable lxdm", false);
    } else if (utils::exec(fmt::format(FMT_COMPILE("{} ly &> /dev/null"), temp), true) == "0") {
        utils::arch_chroot("systemctl enable ly", false);
    }

    /* clang-format off */
    if (zfs == 0) { return; }
    /* clang-format on */

    // if we are using a zfs we should enable the zfs services
    utils::arch_chroot("systemctl enable zfs.target", false);
    utils::arch_chroot("systemctl enable zfs-import-cache", false);
    utils::arch_chroot("systemctl enable zfs-mount", false);
    utils::arch_chroot("systemctl enable zfs-import.target", false);

    // we also need create the cachefile
    utils::exec(fmt::format(FMT_COMPILE("zpool set cachefile=/etc/zfs/zpool.cache $(findmnt {} -lno SOURCE | {}) 2>>/tmp/cachyos-install.log"), mountpoint, "awk -F / '{print $1}'"), true);
    utils::exec(fmt::format(FMT_COMPILE("cp /etc/zfs/zpool.cache {}/etc/zfs/zpool.cache 2>>/tmp/cachyos-install.log"), mountpoint), true);
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
        if (utils::exec(fmt::format(FMT_COMPILE("arch-chroot {} pacman -Qq grub &> /dev/null"), mountpoint), true) != "0") {
            checklist += "- Bootloader is not installed\n";
        }
    }

    // Check if fstab is generated
    if (utils::exec(fmt::format(FMT_COMPILE("grep -qv '^#' {}/etc/fstab 2>/dev/null"), mountpoint), true) != "0") {
        checklist += "- Fstab has not been generated\n";
    }

    // Check if video-driver has been installed
    //[[ ! -e /mnt/.video_installed ]] && echo "- $_GCCheck" >> ${CHECKLIST}

    // Check if locales have been generated
    if (to_int(utils::exec(fmt::format(FMT_COMPILE("arch-chroot {} 'locale -a' | wc -l"), mountpoint), true)) < 3) {
        checklist += "- Locales have not been generated\n";
    }

    // Check if root password has been set
    if (utils::exec("arch-chroot /mnt 'passwd --status root' | cut -d' ' -f2 | grep -q 'NP'", true) == "0") {
        checklist += "- Root password is not set\n";
    }

    // check if user account has been generated
    if (!fs::exists("/mnt/home")) {
        checklist += "- No user accounts have been generated\n";
    }
}

}  // namespace utils
