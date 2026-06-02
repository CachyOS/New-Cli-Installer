#include "cachyos/packages.hpp"

// import gucc
#include "gucc/display_manager.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/initcpio.hpp"
#include "gucc/install.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/package_list.hpp"
#include "gucc/package_profiles.hpp"
#include "gucc/plymouth.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/subprocess.hpp"
#include "gucc/systemd_services.hpp"

#include <expected>     // for unexpected
#include <fstream>      // for ofstream
#include <string>       // for string
#include <string_view>  // for string_view

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

using cachyos::installer::InstallContext;

auto apply_services(const std::vector<gucc::profile::ServiceEntry>& services, std::string_view mountpoint) noexcept -> bool {
    for (const auto& entry : services) {
        if (!gucc::services::systemd_unit_exists(entry.name, mountpoint)) {
            // A service targeted for Disable is already effectively disabled
            // if its unit doesn't exist, then not an error condition
            if (entry.action == gucc::profile::ServiceAction::Disable) {
                spdlog::debug("service '{}' not present, skip disable", entry.name);
                continue;
            }
            if (entry.is_urgent) {
                spdlog::error("required service '{}': unit not found", entry.name);
            } else {
                spdlog::debug("skipping optional service '{}': unit not found", entry.name);
            }
            continue;
        }
        bool ok = false;
        if (entry.is_user_service) {
            ok = gucc::services::enable_user_systemd_service(entry.name, mountpoint);
        } else if (entry.action == gucc::profile::ServiceAction::Disable) {
            ok = gucc::services::disable_systemd_service(entry.name, mountpoint);
        } else {
            ok = gucc::services::enable_systemd_service(entry.name, mountpoint);
        }
        if (ok) {
            const auto& status_str = (entry.action == gucc::profile::ServiceAction::Disable)
                ? "disabled"sv
                : "enabled"sv;
            spdlog::info("{} service '{}'", status_str, entry.name);
        } else if (entry.is_urgent) {
            spdlog::error("failed to configure required service '{}'", entry.name);
            return false;
        } else {
            spdlog::warn("failed to configure optional service '{}'", entry.name);
        }
    }
    return true;
}

auto make_net_profs_info(const InstallContext& ctx) noexcept -> gucc::package::NetProfileInfo {
    return {
        .net_profs_url          = ctx.net_profiles_url,
        .net_profs_fallback_url = ctx.net_profiles_fallback_url,
    };
}

}  // namespace

namespace cachyos::installer {

auto install_base(const InstallContext& ctx, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    const auto& mountpoint = ctx.mountpoint;

    // Get the root filesystem type
    const auto& root_filesystem = gucc::fs::utils::get_mountpoint_fs(mountpoint);
    const auto fs_type          = gucc::fs::string_to_filesystem_type(root_filesystem);

    // Fetch base package list
    const auto net_profs_info = make_net_profs_info(ctx);
    auto pkg_list             = gucc::package::get_pkglist_base(ctx.kernel, root_filesystem, ctx.server_mode, net_profs_info);
    if (!pkg_list.has_value()) {
        return std::unexpected("failed to get base package list");
    }
    const auto& base_pkgs = gucc::utils::join(*pkg_list, ' ');
    spdlog::info("Preparing for pkgs to install: '{}'", base_pkgs);

    spdlog::info("filesystem type on '{}' := '{}', LVM := {}, LUKS := {}", mountpoint, root_filesystem, ctx.crypto.is_lvm, ctx.crypto.is_luks);

    // Build GUCC install config
    const auto install_config = gucc::install::InstallConfig{
        .mountpoint      = mountpoint,
        .packages        = base_pkgs,
        .keymap          = ctx.keymap,
        .initcpio_config = gucc::initcpio::InitcpioConfig{
            .filesystem_type  = fs_type,
            .is_lvm           = ctx.crypto.is_lvm,
            .is_luks          = ctx.crypto.is_luks,
            .use_systemd_hook = true,
        },
        .is_zfs             = !ctx.zfs_zpool_names.empty(),
        .hostcache          = ctx.hostcache,
        .host_files_to_copy = {{"/etc/pacman.conf", "/etc/pacman.conf"}},
    };

    // Run install_base
    if (!gucc::install::install_base(install_config, child)) {
        return std::unexpected("failed to install base");
    }

    // Marker file
    const auto base_installed_path = fmt::format(FMT_COMPILE("{}/.base_installed"), mountpoint);
    std::ofstream{base_installed_path};  // NOLINT

    // Enable base services after base install
    auto svc_result = enable_services(ctx);
    if (!svc_result) {
        spdlog::warn("Failed to enable services: {}", svc_result.error());
    }

    return {};
}

auto install_desktop(std::string_view desktop, const InstallContext& ctx,
    gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    // Fetch desktop package list
    const auto net_profs_info = make_net_profs_info(ctx);
    auto pkg_list             = gucc::package::get_pkglist_desktop(desktop, net_profs_info);
    if (!pkg_list.has_value()) {
        return std::unexpected("failed to get desktop package list");
    }

    spdlog::info("Preparing for desktop envs to install: '{}'", gucc::utils::join(*pkg_list, ' '));

    // Install desktop packages
    const auto& mountpoint = ctx.mountpoint;
    auto pkg_result        = install_packages(*pkg_list, mountpoint, ctx.hostcache, child);
    if (!pkg_result) {
        return std::unexpected(pkg_result.error());
    }

    // Configure plymouth if installed by desktop packages
    if (gucc::plymouth::is_installed(mountpoint)) {
        spdlog::info("Plymouth detected in target, configuring boot splash");

        // Set plymouth theme
        if (!gucc::plymouth::set_default_theme("cachyos-bootanimation"sv, mountpoint)) {
            spdlog::error("Failed to set plymouth theme");
        }

        // Reconfigure initcpio with plymouth hook and rebuild initramfs
        const auto& filesystem_type = gucc::fs::utils::get_mountpoint_fs(mountpoint);
        const auto fs_type          = gucc::fs::string_to_filesystem_type(filesystem_type);

        const auto initcpio_config = gucc::initcpio::InitcpioConfig{
            .filesystem_type  = fs_type,
            .is_lvm           = ctx.crypto.is_lvm,
            .is_luks          = ctx.crypto.is_luks,
            .has_plymouth     = true,
            .use_systemd_hook = true,
        };
        const auto initcpio_path = fmt::format(FMT_COMPILE("{}/etc/mkinitcpio.conf"), mountpoint);
        if (gucc::initcpio::setup_initcpio_config(initcpio_path, initcpio_config)) {
            if (!gucc::utils::arch_chroot_follow("mkinitcpio -P"sv, mountpoint, child)) {
                return std::unexpected("failed to rebuild initramfs with plymouth hook");
            }
        } else {
            spdlog::error("Failed to reconfigure initcpio with plymouth hook");
        }
    }

    // Additionally call it to enable desktop services
    auto svc_result = enable_services(ctx);
    if (!svc_result) {
        spdlog::warn("Failed to enable services: {}", svc_result.error());
    }
    return {};
}

auto install_packages(const std::vector<std::string>& packages,
    std::string_view mountpoint, bool hostcache,
    gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    /* clang-format off */
    if (packages.empty()) { return {}; }
    /* clang-format on */

    const auto& pkgs_str      = gucc::utils::join(packages, ' ');
    const auto& cmd           = hostcache ? "pacstrap" : "pacstrap -c";
    const auto& cmd_formatted = fmt::format(FMT_COMPILE("{} {} {} |& tee -a /tmp/pacstrap.log"), cmd, mountpoint, pkgs_str);

    if (!gucc::utils::exec_follow({"/bin/sh", "-c", cmd_formatted}, child)) {
        return std::unexpected(fmt::format("failed to install packages: {}", pkgs_str));
    }
    return {};
}

auto remove_packages(const std::vector<std::string>& packages,
    std::string_view mountpoint, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    /* clang-format off */
    if (packages.empty()) { return {}; }
    /* clang-format on */

    const auto& pkgs_str       = gucc::utils::join(packages, ' ');
    const auto& chroot_command = fmt::format(FMT_COMPILE("pacman -Rsn {}"), pkgs_str);
    if (!gucc::utils::arch_chroot_follow(chroot_command, mountpoint, child)) {
        return std::unexpected(fmt::format("failed to remove packages: {}", pkgs_str));
    }
    return {};
}

auto enable_services(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string> {
    spdlog::info("Enabling services...");
    const auto net_profs_info = make_net_profs_info(ctx);
    const auto& base_services = gucc::package::get_servicelist_base(ctx.server_mode, net_profs_info);
    if (!base_services) {
        return std::unexpected("failed to get base service list");
    }

    const auto& mountpoint = ctx.mountpoint;
    apply_services(*base_services, mountpoint);
    if (!ctx.server_mode) {
        const auto& desktop_services = gucc::package::get_servicelist_desktop(net_profs_info);
        if (!desktop_services) {
            return std::unexpected("failed to get desktop service list");
        }
        apply_services(*desktop_services, mountpoint);
    }

    // Display manager detection (desktop mode only)
    if (!ctx.server_mode) {
        const auto detected = gucc::display_manager::detect_installed(mountpoint);
        if (!detected) {
            spdlog::debug("No display manager units found on target yet. Skipping DM enablement..");
        } else if (!gucc::display_manager::enable(*detected, mountpoint)) {
            spdlog::error("Failed to enable display manager '{}'", gucc::display_manager::to_string(*detected));
        }
        if (detected == gucc::display_manager::Kind::Lightdm) {
            gucc::display_manager::configure_lightdm_greeter(mountpoint);
        }
    }

    return {};
}

auto install_needed(std::string_view pkg, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    // Check if already installed
    if (gucc::utils::exec_checked(fmt::format(FMT_COMPILE("pacman -Qq {} &>/dev/null"), pkg))) {
        return {};
    }

    // Install it
    const auto& cmd = fmt::format(FMT_COMPILE("pacman -Sy --noconfirm {}"), pkg);
    if (!gucc::utils::exec_follow({"/bin/sh", "-c", cmd}, child)) {
        return std::unexpected(fmt::format("failed to install needed package: {}", pkg));
    }
    return {};
}

}  // namespace cachyos::installer
