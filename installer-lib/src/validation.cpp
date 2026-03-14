#include "cachyos/validation.hpp"

#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"

#include <charconv>     // for from_chars
#include <filesystem>   // for exists
#include <string>       // for string
#include <string_view>  // for string_view

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace {
template <typename T = std::int32_t>
    requires std::numeric_limits<T>::is_integer
inline T to_int(const std::string_view& str) {
    T result = 0;
    std::from_chars(str.data(), str.data() + str.size(), result);
    return result;
}
}  // namespace

namespace cachyos::installer {

auto check_mount(std::string_view mountpoint) noexcept -> bool {
    return !gucc::fs::utils::get_mountpoint_source(mountpoint).empty();
}

auto check_base_installed(std::string_view mountpoint) noexcept -> bool {
    if (!check_mount(mountpoint)) {
        return false;
    }

    // TODO(vnepogodin): invalidate base install
    // checking just for pacman is not enough
    const auto& pacman_path = fmt::format(FMT_COMPILE("{}/usr/bin/pacman"), mountpoint);
    return fs::exists(pacman_path);
}

auto final_check(const InstallContext& ctx) noexcept -> ValidationResult {
    ValidationResult result{.success = true, .errors = {}, .warnings = {}};

    // Check if base is installed
    const auto& base_marker = fmt::format(FMT_COMPILE("{}/.base_installed"), ctx.mountpoint);
    if (!fs::exists(base_marker)) {
        result.success = false;
        result.errors.emplace_back("Base is not installed");
        return result;
    }

    // Check if bootloader is installed (BIOS only)
    // TODO(vnepogodin): should check for all bootloaders
    if (ctx.system_mode == InstallContext::SystemMode::BIOS) {
        if (!gucc::utils::exec_checked(fmt::format(FMT_COMPILE("arch-chroot {} pacman -Qq grub &> /dev/null"), ctx.mountpoint))) {
            result.success = false;
            result.errors.emplace_back("Bootloader is not installed");
        }
    }

    // Check if fstab is generated
    if (!gucc::utils::exec_checked(fmt::format(FMT_COMPILE("grep -qv '^#' {}/etc/fstab 2>/dev/null"), ctx.mountpoint))) {
        result.success = false;
        result.errors.emplace_back("Fstab has not been generated");
    }

    // Check if locales have been generated
    const auto& locale_count_str = gucc::utils::exec(fmt::format(FMT_COMPILE("arch-chroot {} locale -a | wc -l"), ctx.mountpoint), false);
    if (to_int(locale_count_str) < 3) {
        result.success = false;
        result.errors.emplace_back("Locales have not been generated");
    }

    // Check if root password has been set
    if (gucc::utils::exec_checked(fmt::format(FMT_COMPILE("arch-chroot {} passwd --status root | cut -d' ' -f2 | grep -q 'NP'"), ctx.mountpoint))) {
        result.success = false;
        result.errors.emplace_back("Root password is not set");
    }

    // Check if user account has been generated
    const auto& home_path = fmt::format(FMT_COMPILE("{}/home"), ctx.mountpoint);
    if (!fs::exists(home_path)) {
        result.warnings.emplace_back("No user accounts have been generated");
    }

    return result;
}

}  // namespace cachyos::installer
