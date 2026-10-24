#include "cachyos/validation.hpp"

// import gucc
#include "gucc/bootloader.hpp"
#include "gucc/conf.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <cstddef>  // for size_t

#include <algorithm>    // for any_of, contains, count_if
#include <array>        // for array
#include <filesystem>   // for exists
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace {
[[nodiscard]] auto bootloader_installed(const cachyos::installer::InstallContext& ctx) noexcept -> bool {
    using gucc::bootloader::BootloaderType;
    switch (ctx.bootloader) {
    case BootloaderType::Grub:
        return gucc::utils::arch_chroot_query_checked("pacman -Qq grub"sv, ctx.mountpoint);
    case BootloaderType::Refind:
        return gucc::utils::arch_chroot_query_checked("pacman -Qq refind"sv, ctx.mountpoint);
    case BootloaderType::Limine:
        return gucc::utils::arch_chroot_query_checked("pacman -Qq limine"sv, ctx.mountpoint);
    case BootloaderType::SystemdBoot:
        // Check both common ESP mount points (loader.conf lives at ESP root).
        return fs::exists(gucc::utils::target_path(ctx.mountpoint, "/boot/loader/loader.conf"sv))
            || fs::exists(gucc::utils::target_path(ctx.mountpoint, "/boot/efi/loader/loader.conf"sv));
    }
    return false;
}
}  // namespace

namespace cachyos::installer::detail {

auto count_generated_locales(std::string_view locale_a_output) noexcept -> std::size_t {
    static constexpr std::array kBuiltin{"C"sv, "C.utf8"sv, "C.UTF-8"sv, "POSIX"sv};
    return static_cast<std::size_t>(std::ranges::count_if(gucc::utils::nonblank_lines(locale_a_output), [](std::string_view line) {
        return !std::ranges::contains(kBuiltin, line);
    }));
}

}  // namespace cachyos::installer::detail

namespace cachyos::installer {

auto fstab_has_entries(std::string_view fstab_content) noexcept -> bool {
    return std::ranges::any_of(gucc::utils::split_lines(fstab_content), [](std::string_view line) { return gucc::conf::active(line).has_value(); });
}

auto shadow_password_empty(std::string_view shadow_content, std::string_view user) noexcept -> bool {
    const auto fields = gucc::utils::value_of(gucc::utils::make_split_view(shadow_content), user, ':');
    if (!fields) {
        return true;
    }
    const auto password = fields->substr(0, fields->find(':'));
    static constexpr std::array kNoPassword{""sv, "*"sv, "!"sv, "!!"sv, "!*"sv};
    return std::ranges::contains(kNoPassword, password);
}

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
        if (!fs::exists(gucc::utils::target_path(mountpoint, rel))) {
            return false;
        }
    }
    return true;
}

auto final_check(const InstallContext& ctx) noexcept -> ValidationResult {
    ValidationResult result{.success = true, .errors = {}, .warnings = {}};

    // Check if base is installed
    const auto& base_marker = gucc::utils::target_path(ctx.mountpoint, "/.base_installed"sv);
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
    if (!fstab_has_entries(gucc::file_utils::read_whole_file(gucc::utils::target_path(ctx.mountpoint, "/etc/fstab"sv)))) {
        result.success = false;
        result.errors.emplace_back("Fstab has not been generated");
    }

    // Check if locales have been generated
    if (detail::count_generated_locales(gucc::utils::arch_chroot_query("locale -a"sv, ctx.mountpoint)) == 0) {
        result.success = false;
        result.errors.emplace_back("Locales have not been generated");
    }

    // Check if root password has been set
    if (shadow_password_empty(gucc::file_utils::read_whole_file(gucc::utils::target_path(ctx.mountpoint, "/etc/shadow"sv)), "root"sv)) {
        result.success = false;
        result.errors.emplace_back("Root password is not set");
    }

    // Check if user account has been generated
    const auto& home_path = gucc::utils::target_path(ctx.mountpoint, "/home"sv);
    if (!fs::exists(home_path)) {
        result.warnings.emplace_back("No user accounts have been generated");
    }

    return result;
}

}  // namespace cachyos::installer
