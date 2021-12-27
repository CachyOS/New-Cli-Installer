#include "utils.hpp"
#include "config.hpp"
#include "definitions.hpp"
#include "follow_process_log.hpp"
#include "subprocess.h"
#include "tui.hpp"
#include "widgets.hpp"

#include <algorithm>      // for transform
#include <array>          // for array
#include <chrono>         // for filesystem, seconds
#include <cstdint>        // for int32_t
#include <cstdio>         // for feof, fgets, pclose, perror, popen
#include <cstdlib>        // for exit, WIFEXITED, WIFSIGNALED
#include <filesystem>     // for exists, is_directory
#include <iostream>       // for basic_istream, cin
#include <regex>          // for regex_search, match_results<>::_Base_type
#include <string>         // for operator==, string, basic_string, allocator
#include <sys/mount.h>    // for mount
#include <sys/wait.h>     // for waitpid
#include <thread>         // for sleep_for
#include <unistd.h>       // for execvp, fork
#include <unordered_map>  // for unordered_map

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"

#include <range/v3/algorithm/for_each.hpp>
#include <range/v3/algorithm/reverse.hpp>
#include <range/v3/core.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/split.hpp>
#include <range/v3/view/transform.hpp>

#pragma clang diagnostic pop
#else
#include <ranges>
namespace ranges = std::ranges;
#endif

#ifdef NDEVENV
#include <cpr/api.h>
#include <cpr/response.h>
#include <cpr/status_codes.h>
#include <cpr/timeout.h>
#endif

namespace fs = std::filesystem;

namespace utils {
static constexpr std::int32_t CONNECTION_TIMEOUT = 15;

bool is_connected() noexcept {
#ifdef NDEVENV
    /* clang-format off */
    auto r = cpr::Get(cpr::Url{"https://www.google.com"},
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
    const auto& cmd_formatted = fmt::format("arch-chroot {} \"{}\"", mountpoint, command);
    if (follow) {
        tui::detail::follow_process_log_widget({"/sbin/bash", "-c", fmt::format("\"{}\"", cmd_formatted)});
        return;
    }
    utils::exec(cmd_formatted);
}

void exec_follow(const std::vector<std::string>& vec, std::string& process_log, bool& running, subprocess_s& child, bool async) noexcept {
    if (!async) {
        spdlog::error("Implement me!");
        return;
    }
    subprocess_s process{};
    int32_t ret{-1};
    static std::array<char, 1048576 + 1> data{};
    uint32_t index{};
    uint32_t bytes_read{};

    std::vector<char*> args;
    std::transform(vec.cbegin(), vec.cend(), std::back_inserter(args),
        [=](const std::string& arg) -> char* { return std::bit_cast<char*>(arg.data()); });
    args.push_back(nullptr);

    std::memset(data.data(), 0, data.size());
    char** command                             = args.data();
    static constexpr const char* environment[] = {"PATH=/sbin:/bin:/usr/local/sbin:/usr/local/bin:/usr/bin:/usr/sbin", nullptr};
    if ((ret = subprocess_create_ex(command, subprocess_option_enable_async | subprocess_option_combined_stdout_stderr, environment, &process)) != 0) {
        running = false;
        return;
    }

    child = process;

    do {
        bytes_read = subprocess_read_stdout(&process, data.data() + index,
            static_cast<uint32_t>(data.size()) - 1 - index);

        index += bytes_read;
        process_log = data.data();
    } while (bytes_read != 0);

    subprocess_join(&process, &ret);
    subprocess_destroy(&process);
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
std::string exec(const std::string_view& command, const bool& interactive) noexcept(false) {
    if (interactive) {
        const auto& ret_code = system(command.data());
        return std::to_string(ret_code);
    }

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.data(), "r"), pclose);

    if (!pipe)
        throw std::runtime_error("popen failed!");

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
        char read_char{};
        if (tmp.length() == 1) {
            read_char = tmp[0];
        } else {
            read_char = '\0';
        }

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
void inst_needed(const std::string_view& pkg) {
    const auto& pkg_info = utils::exec(fmt::format("pacman -Q {}", pkg));
    const std::regex pkg_regex("/error/");
    if (!std::regex_search(pkg_info, pkg_regex)) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        utils::clear_screen();
        utils::exec(fmt::format("pacman -Sy --noconfirm {}", pkg));
    }
}

// Unmount partitions.
void umount_partitions() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    auto mount_info             = utils::exec(fmt::format("mount | grep \"{}\" | {}", mountpoint_info, "awk \'{print $3}\' | sort -r"));
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
    utils::exec(fmt::format("wipe -Ifre {}", device_info));
#else
    utils::inst_needed("bash");
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
    const std::string other_piece = "sed \'s/part$/\\/dev\\//g\' | sed \'s/lvm$\\|crypt$/\\/dev\\/mapper\\//g\' | awk \'{print $3$1 \" \" $2}\' | awk \'!/mapper/{a[++i]=$0;next}1;END{while(x<length(a))print a[++x]}\' ; zfs list -Ht volume -o name,volsize 2>/dev/null | awk \'{printf \"/dev/zvol/%s %s\\n\", $1, $2}\'";
    auto partition_list           = utils::exec(fmt::format("lsblk -lno NAME,SIZE,TYPE | grep {} | {}", include_part, other_piece));

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

    const auto& partitions    = utils::make_multiline(partition_list, true);
    config_data["PARTITIONS"] = partitions;
    number_partitions         = static_cast<std::int32_t>(partitions.size()) - 1;

    // Double-partitions will be counted due to counting sizes, so fix
    number_partitions /= 2;

    // for test delete /dev:sda8
    // delete_partition_in_list "/dev/sda8"

    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    // Deal with partitioning schemes appropriate to mounting, lvm, and/or luks.
    if (include_part == "\'part\\|lvm\\|crypt\'" && (((system_info == "UEFI") && (number_partitions < 2)) || ((system_info == "BIOS") && (number_partitions == 0)))) {
        // Deal with incorrect partitioning for main mounting function
        tui::create_partitions();
    } else if (include_part == "\'part\\|crypt\'" && number_partitions == 0) {
        // Ensure there is at least one partition for LVM
        tui::create_partitions();
    } else if (include_part == "\'part\\|lvm\'" && number_partitions < 2) {
        // Ensure there are at least two partitions for LUKS
        tui::create_partitions();
    }
}

// List partitions to be hidden from the mounting menu
std::string list_mounted() noexcept {
    utils::exec("lsblk -l | awk '$7 ~ /mnt/ {print $1}' > /tmp/.mounted");
    return utils::exec("echo /dev/* /dev/mapper/* | xargs -n1 2>/dev/null | grep -f /tmp/.mounted");
}

std::string list_containing_crypt() noexcept {
    return utils::exec("blkid | awk \'/TYPE=\"crypto_LUKS\"/{print $1}\' | sed 's/.$//\'");
}

std::string list_non_crypt() noexcept {
    return utils::exec("blkid | awk \'!/TYPE=\"crypto_LUKS\"/{print $1}\' | sed \'s/.$//\'");
}

// Ensure that a partition is mounted
bool check_mount() noexcept {
#ifdef NDEVENV
    auto* config_instance       = Config::instance();
    auto& config_data           = config_instance->data();
    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    if (utils::exec(fmt::format("findmnt -nl {}", mountpoint_info)) == "") {
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

bool handle_connection() noexcept {
    bool connected{};

    if (!(connected = utils::is_connected())) {
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
        utils::exec("yes | pacman -Sy --noconfirm", true);
    }

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

}  // namespace utils
