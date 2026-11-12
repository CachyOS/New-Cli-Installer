#pragma once

#include "cachyos/export.hpp"

#include "cachyos/types.hpp"

#include <cstdint>  // for int32_t

#include <expected>     // for expected
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

// the individual install steps. the orchestrator runs them in order with progress, but you can
// call any of them on their own as long as its preconditions hold.
CACHYOS_INSTALLER_EXPORT_BEGIN

namespace cachyos::installer::steps {

[[nodiscard]] auto needs_umount(const InstallContext& ctx) noexcept -> bool;

/// Whenever should install desktop profiles on server edition.
[[nodiscard]] auto has_desktop(const InstallContext& ctx) noexcept -> bool;

/// Unmount any existing partitions on the install target.
[[nodiscard]] auto umount(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Prepare the target according to part mode.
[[nodiscard]] auto partition(InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Generate fstab inside the target.
[[nodiscard]] auto fstab(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Configure LUKS-encrypted swap when @ref InstallContext::encrypt_swap is set
/// and a swap device is known. No-op otherwise.
[[nodiscard]] auto encrypt_swap(const InstallContext& ctx) noexcept
    -> std::vector<std::string>;

/// Apply hostname/locale/keymap/timezone/hw_clock to the target system.
[[nodiscard]] auto system_settings(const SystemSettings& sys,
    const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Set the root password and create the primary user.
[[nodiscard]] auto users(const UserSettings& user,
    std::string_view root_password,
    const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Installs the base system into the target. Initramfs generation is deferred to @ref initramfs.
[[nodiscard]] auto base(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Generate machine id on the target.
[[nodiscard]] auto machine_id(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Install the selected desktop environment packages.
/// No-op in server mode or when no desktop is selected.
[[nodiscard]] auto desktop(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Post-pacstrap desktop configuration.
/// No-op in server mode or when no desktop is selected.
[[nodiscard]] auto desktop_configure(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Install server packages.
[[nodiscard]] auto server_packages(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Setup ssh access.
[[nodiscard]] auto ssh_keys(const UserSettings& user,
    const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Setup firewall ports.
[[nodiscard]] auto server_firewall(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Configure the installed display manager to autologin @p user.
[[nodiscard]] auto autologin(const UserSettings& user,
    const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Install every chwd hardware-driver profile applicable to the target's
/// detected hardware.
[[nodiscard]] auto chwd(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Carry the live ISO's NetworkManager profiles into the target.
/// Returns the number of profiles copied; -1 on I/O failure.
auto network_carryover(const InstallContext& ctx) noexcept -> std::int32_t;

/// Builds initramfs image.
[[nodiscard]] auto initramfs(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Install the configured bootloader.
[[nodiscard]] auto bootloader(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Drop the mkinitcpio deferral hook so the installed system rebuilds images on kernel updates.
[[nodiscard]] auto restore_initramfs_hook(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Probe the mounted target's root crypto layout and update @p ctx.crypto in place.
auto detect_crypto(InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Enable systemd services inside the target.
[[nodiscard]] auto enable_services(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Create a permanent btrfs installation snapshot.
[[nodiscard]] auto btrfs_snapshot(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

// final sanity checks on the installed system. both errors and warnings on the returned
// ValidationResult are non-fatal as far as the orchestrator cares.
[[nodiscard]] auto final_validation(const InstallContext& ctx) noexcept
    -> ValidationResult;

/// Run @ref InstallContext::post_install_script on the host with the target root as $1.
[[nodiscard]] auto post_install(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Restore the initramfs hook, copy log file into the target, unmount
/// partitions and close the root mapper. Also the teardown of a failed or cancelled run.
[[nodiscard]] auto cleanup(const InstallContext& ctx) noexcept
    -> std::vector<std::string>;

}  // namespace cachyos::installer::steps

CACHYOS_INSTALLER_EXPORT_END
