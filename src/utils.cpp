#include "utils.hpp"

#include <sys/mount.h>

#include <filesystem>
#include <iostream>
#include <string>

#include <netdb.h>

namespace fs = std::filesystem;

static std::string_view H_INIT{"openrc"};
static std::string_view SYSTEM{"BIOS"};
static std::string_view NW_CMD{};

namespace utils {
bool is_connected() noexcept {
    return gethostbyname("google.com");
}

bool check_root() noexcept {
    return (utils::exec("whoami") == "root\n");
}

void clear_screen() noexcept {
    static constexpr auto CLEAR_SCREEN_ANSI = "\033[1;1H\033[2J";
    write(STDOUT_FILENO, CLEAR_SCREEN_ANSI, 11);
}

std::string exec(const std::string_view& command) noexcept {
    char buffer[128];
    std::string result;

    FILE* pipe = popen(command.data(), "r");

    if (!pipe) {
        return "popen failed!";
    }

    while (!feof(pipe)) {
        if (fgets(buffer, 128, pipe) != nullptr) {
            result += buffer;
        }
    }

    pclose(pipe);

    return result;
}

bool prompt_char(const char* prompt, const char* color, char* read) noexcept {
    fmt::print("{}{}{}\n", color, prompt, RESET);

    std::string tmp;
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

void id_system() noexcept {
    // Apple System Detection
    const auto& sys_vendor = utils::exec("cat /sys/class/dmi/id/sys_vendor");
    if ((sys_vendor == "Apple Inc.\n") || (sys_vendor == "Apple Computer, Inc.\n"))
        utils::exec("modprobe -r -q efivars || true");  // if MAC
    else
        utils::exec("modprobe -q efivarfs");  // all others

    // BIOS or UEFI Detection
    static constexpr auto efi_path = "/sys/firmware/efi/";
    if (fs::exists(efi_path) && fs::is_directory(efi_path)) {
        // Mount efivarfs if it is not already mounted
        const auto& mount_out = utils::exec("mount | grep /sys/firmware/efi/efivars");
        if (mount_out == "\n") {
            if (mount("efivarfs", "/sys/firmware/efi/efivars", "efivarfs", 0, "") != 0) {
                perror("utils::id_system");
                exit(1);
            }
        }
        SYSTEM = "UEFI";
    }

    // init system
    const auto& init_sys = utils::exec("cat /proc/1/comm");
    if (init_sys == "systemd\n")
        H_INIT = "systemd";

    // TODO: Test which nw-client is available, including if the service according to $H_INIT is running
    if (H_INIT == "systemd" && utils::exec("systemctl is-active NetworkManager") == "active\n")
        NW_CMD = "nmtui";
}

}  // namespace utils
