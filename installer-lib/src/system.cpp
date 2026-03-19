#include "cachyos/system.hpp"

#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/pacmanconf_repo.hpp"
#include "gucc/repos.hpp"

#include <sys/mount.h>  // for mount
#include <unistd.h>     // for getuid

#include <chrono>      // for seconds
#include <expected>    // for unexpected
#include <filesystem>  // for exists, is_directory
#include <fstream>     // for ifstream
#include <string>      // for string
#include <thread>      // for sleep_for

#include <fmt/compile.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
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
    // TODO(vnepogodin): refactor later to util function to read just single line
    std::string sys_vendor{};
    if (std::ifstream ifs{"/sys/class/dmi/id/sys_vendor"}; ifs) {
        std::getline(ifs, sys_vendor);
    }
    if ((sys_vendor == "Apple Inc."sv) || (sys_vendor == "Apple Computer, Inc."sv)) {
        if (!gucc::utils::exec_checked("modprobe -r -q efivars"sv)) {
            spdlog::warn("failed to unload efivars module");
        }
    } else {
        if (!gucc::utils::exec_checked("modprobe -q efivarfs"sv)) {
            spdlog::warn("failed to load efivarfs module");
        }
    }

    // BIOS or UEFI Detection
    static constexpr auto efi_path = "/sys/firmware/efi/";
    if (fs::exists(efi_path) && fs::is_directory(efi_path)) {
        // Mount efivarfs if it is not already mounted
        static constexpr auto efivars_path = "/sys/firmware/efi/efivars";
        if (::gucc::fs::utils::get_mountpoint_source(efivars_path).empty()) {
            if (mount("efivarfs", efivars_path, "efivarfs", 0, "") != 0) {
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
    // surely root user will always be 0
    return getuid() == 0;
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

    const auto limit = timeout_secs.count();
    spdlog::warn("No active network connection detected, waiting {} seconds...", limit);
    for (std::int64_t waited{}; waited < limit; ++waited) {
        std::this_thread::sleep_for(1s);
        if (is_connected()) {
            return true;
        }
    }
    return false;
}

auto install_cachyos_repo() noexcept -> std::expected<void, std::string> {
    // Check if it's already been applied
    const auto& repo_list = gucc::detail::pacmanconf::get_repo_list("/etc/pacman.conf");
    spdlog::info("install_cachyos_repo: repo_list := '{}'", repo_list);

    if (!gucc::repos::install_cachyos_repos()) {
        return std::unexpected("failed to install cachyos repos");
    }
    if (!gucc::utils::exec_checked("yes | pacman -Sy --noconfirm"sv)) {
        return std::unexpected("failed to refresh pacman databases");
    }
    return {};
}

}  // namespace cachyos::installer
