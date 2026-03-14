#include "cachyos/system.hpp"

#include "gucc/io_utils.hpp"

#include <chrono>      // for seconds
#include <expected>    // for unexpected
#include <filesystem>  // for exists, is_directory
#include <string>      // for string
#include <thread>      // for sleep_for

#include <sys/mount.h>  // for mount

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <cpr/api.h>
#include <cpr/response.h>
#include <cpr/status_codes.h>
#include <cpr/timeout.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;
using namespace std::chrono_literals;

namespace cachyos::installer {

auto detect_system() noexcept -> std::expected<SystemInfo, std::string> {
    SystemInfo info{};

    // Apple System Detection
    const auto& sys_vendor = gucc::utils::exec("cat /sys/class/dmi/id/sys_vendor");
    if ((sys_vendor == "Apple Inc."sv) || (sys_vendor == "Apple Computer, Inc."sv)) {
        gucc::utils::exec("modprobe -r -q efivars &>/dev/null");
    } else {
        gucc::utils::exec("modprobe -q efivarfs &>/dev/null");
    }

    // BIOS or UEFI Detection
    static constexpr auto efi_path = "/sys/firmware/efi/";
    if (fs::exists(efi_path) && fs::is_directory(efi_path)) {
        // Mount efivarfs if it is not already mounted
        const auto& mount_out = gucc::utils::exec("mount | grep /sys/firmware/efi/efivars");
        if (mount_out.empty()) {
            if (mount("efivarfs", "/sys/firmware/efi/efivars", "efivarfs", 0, "") != 0) {
                return std::unexpected(fmt::format(FMT_COMPILE("failed to mount efivarfs: {}"), std::strerror(errno)));
            }
        }
        info.system_mode = InstallContext::SystemMode::UEFI;
    } else {
        info.system_mode = InstallContext::SystemMode::BIOS;
    }

    return info;
}

auto check_root() noexcept -> bool {
    return (gucc::utils::exec("whoami") == "root"sv);
}

auto is_connected() noexcept -> bool {
    /* clang-format off */
    auto r = cpr::Get(cpr::Url{"https://cachyos.org"},
             cpr::Timeout{15s});
    /* clang-format on */
    return cpr::status::is_success(static_cast<std::int32_t>(r.status_code))
        || cpr::status::is_redirect(static_cast<std::int32_t>(r.status_code));
}

auto wait_for_connection(const std::chrono::seconds& timeout_secs) noexcept -> bool {
    if (is_connected()) {
        return true;
    }

    std::int64_t waited{};
    const auto limit = timeout_secs.count();
    spdlog::warn("No active network connection detected, waiting {} seconds...", limit);
    while (!is_connected()) {
        std::this_thread::sleep_for(1s);
        if (++waited >= limit) {
            break;
        }
    }

    return is_connected();
}

}  // namespace cachyos::installer
