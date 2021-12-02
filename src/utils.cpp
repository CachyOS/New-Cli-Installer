#include "utils.hpp"
#include "config.hpp"
#include "definitions.hpp"

#include <array>          // for array
#include <chrono>         // for filesystem, seconds
#include <cstdint>        // for int32_t
#include <cstdio>         // for feof, fgets, pclose, perror, popen
#include <cstdlib>        // for exit, WIFEXITED, WIFSIGNALED
#include <filesystem>     // for exists, is_directory
#include <iostream>       // for basic_istream, cin
#include <string>         // for operator==, string, basic_string, allocator
#include <sys/mount.h>    // for mount
#include <sys/wait.h>     // for waitpid
#include <thread>         // for sleep_for
#include <unistd.h>       // for execvp, fork
#include <unordered_map>  // for unordered_map

#include <cpr/api.h>
#include <cpr/response.h>
#include <cpr/status_codes.h>
#include <cpr/timeout.h>

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
    output("{}", CLEAR_SCREEN_ANSI);
}

std::string exec(const std::string_view& command, bool capture_output) noexcept {
    if (!capture_output) {
        std::int32_t status{};
        auto pid = fork();
        if (pid == 0) {
            /* clang-format off */
            char* args[2] = { const_cast<char*>(command.data()), nullptr };
            /* clang-format on */
            execvp(args[0], args);
        } else {
            do {
                waitpid(pid, &status, 0);
            } while ((!WIFEXITED(status)) && (!WIFSIGNALED(status)));
        }
        return {};
    }
    auto* pipe = popen(command.data(), "r");
    if (!pipe) {
        return "popen failed!";
    }

    std::string result{};
    std::array<char, 128> buffer{};
    while (!feof(pipe)) {
        if (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            result += buffer.data();
        }
    }

    if (result.ends_with('\n')) {
        result.pop_back();
    }

    pclose(pipe);

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

auto make_multiline(std::string& str) noexcept -> std::vector<std::string> {
    static constexpr std::string_view delim{"\n"};
    std::vector<std::string> lines{};

    std::size_t start{};
    std::size_t end = str.find(delim);
    while (end != std::string::npos) {
        lines.push_back(str.substr(start, end - start));
        start = end + delim.size();
        end   = str.find(delim, start);
    }
    lines.push_back(str.substr(start, end - start));

    return lines;
}

// Unmount partitions.
void umount_partitions() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    auto mount_info       = utils::exec(fmt::format("mount | grep \"{}\" | {}", config_data["MOUNTPOINT"], "awk \'{print $3}\' | sort -r"));
#ifdef NDEVENV
    utils::exec("swapoff -a");
#endif

    const auto& lines = utils::make_multiline(mount_info);
    for (const auto& line : lines) {
#ifdef NDEVENV
        umount(line.c_str());
#else
        output("{}\n", line);
#endif
    }
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
    if (init_sys == "systemd")
        config_data["H_INIT"] = "systemd";

    // TODO: Test which nw-client is available, including if the service according to $H_INIT is running
    if (config_data["H_INIT"] == "systemd" && utils::exec("systemctl is-active NetworkManager") == "active")
        config_data["NW_CMD"] = "nmtui";
}

bool handle_connection() noexcept {
    bool connected{};

    if (!(connected = utils::is_connected())) {
        warning("An active network connection could not be detected, waiting 15 seconds ...\n");

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

    return connected;
}

void show_iwctl() noexcept {
    info("\nInstructions to connect to wifi using iwctl:\n");
    info("1 - To find your wifi device name (ex: wlan0) type `device list`\n");
    info("2 - type `station wlan0 scan`, and wait couple seconds\n");
    info("3 - type `station wlan0 get-networks` (find your wifi Network name ex. my_wifi)\n");
    info("4 - type `station wlan0 connect my_wifi` (don't forget to press TAB for auto completion!\n");
    info("5 - type `station wlan0 show` (status should be connected)\n");
    info("6 - type `exit`\n");

    while (utils::prompt_char("Press a key to continue...", CYAN)) {
        utils::exec("iwctl", false);
        break;
    }
}

}  // namespace utils
