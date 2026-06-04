#include "cachyos/validation.hpp"

// import gucc
#include "gucc/bootloader.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"

#include <array>        // for array
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

[[nodiscard]] auto bootloader_installed(const cachyos::installer::InstallContext& ctx) noexcept -> bool {
    using gucc::bootloader::BootloaderType;
    switch (ctx.bootloader) {
    case BootloaderType::Grub:
        return gucc::utils::exec_checked(
            fmt::format(FMT_COMPILE("arch-chroot {} pacman -Qq grub &> /dev/null"), ctx.mountpoint));
    case BootloaderType::Refind:
        return gucc::utils::exec_checked(
            fmt::format(FMT_COMPILE("arch-chroot {} pacman -Qq refind &> /dev/null"), ctx.mountpoint));
    case BootloaderType::Limine:
        return gucc::utils::exec_checked(
            fmt::format(FMT_COMPILE("arch-chroot {} pacman -Qq limine &> /dev/null"), ctx.mountpoint));
    case BootloaderType::SystemdBoot:
        // Check both common ESP mount points (loader.conf lives at ESP root).
        return fs::exists(fmt::format(FMT_COMPILE("{}/boot/loader/loader.conf"), ctx.mountpoint))
            || fs::exists(fmt::format(FMT_COMPILE("{}/boot/efi/loader/loader.conf"), ctx.mountpoint));
    }
    return false;
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

    // A complete base install ships at least these. Missing any one means
    // pacstrap didn't finish or the target FS was wiped between steps.
    static constexpr std::array<std::string_view, 5> kBaseMarkers{
        "/usr/bin/pacman"sv,
        "/usr/bin/bash"sv,
        "/usr/bin/systemctl"sv,
        "/etc/pacman.conf"sv,
        "/etc/passwd"sv,
    };
    for (const auto& rel : kBaseMarkers) {
        if (!fs::exists(fmt::format(FMT_COMPILE("{}{}"), mountpoint, rel))) {
            return false;
        }
    }
    return true;
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

    // Check if the configured bootloader is installed.
    if (!bootloader_installed(ctx)) {
        result.success = false;
        result.errors.emplace_back("Bootloader is not installed");
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
