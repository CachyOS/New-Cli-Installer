#ifndef CACHYOS_INSTALLER_BOOTLOADER_HPP
#define CACHYOS_INSTALLER_BOOTLOADER_HPP

#include "cachyos/types.hpp"

// import gucc
#include "gucc/bootloader.hpp"

#include <expected>     // for expected
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace cachyos::installer {

/// Installs the bootloader selected in the context.
[[nodiscard]] auto install_bootloader(const InstallContext& ctx,
    const ExecutionCallbacks& callbacks) noexcept
    -> std::expected<void, std::string>;

/// Installs GRUB in UEFI mode.
[[nodiscard]] auto install_grub_uefi(const InstallContext& ctx, std::string_view bootid,
    bool as_default, const ExecutionCallbacks& callbacks) noexcept
    -> std::expected<void, std::string>;

/// Installs GRUB in BIOS mode.
[[nodiscard]] auto install_grub_bios(const InstallContext& ctx,
    const ExecutionCallbacks& callbacks) noexcept
    -> std::expected<void, std::string>;

/// Installs rEFInd bootloader.
[[nodiscard]] auto install_refind(const InstallContext& ctx,
    const ExecutionCallbacks& callbacks) noexcept
    -> std::expected<void, std::string>;

/// Installs systemd-boot.
[[nodiscard]] auto install_systemd_boot(const InstallContext& ctx,
    const ExecutionCallbacks& callbacks) noexcept
    -> std::expected<void, std::string>;

/// Installs Limine bootloader.
[[nodiscard]] auto install_limine(const InstallContext& ctx,
    const ExecutionCallbacks& callbacks) noexcept
    -> std::expected<void, std::string>;

/// Builds the kernel command-line parameters string.
[[nodiscard]] auto get_kernel_params(const InstallContext& ctx) noexcept
    -> std::expected<std::vector<std::string>, std::string>;

/// Runs grub-mkconfig inside the chroot.
[[nodiscard]] auto grub_mkconfig(std::string_view mountpoint,
    const ExecutionCallbacks& callbacks) noexcept
    -> std::expected<void, std::string>;

/// Applies common GRUB configuration (cmdline, crypto, etc.).
void configure_grub_common(gucc::bootloader::GrubConfig& grub_config,
    gucc::bootloader::GrubInstallConfig& grub_install_config,
    std::string_view mountpoint,
    std::string_view luks_dev,
    std::string_view zfs_extra_pkgs,
    std::string_view non_zfs_extra_pkgs) noexcept;

/// Runs a command inside the target chroot.
[[nodiscard]] auto arch_chroot(std::string_view command, std::string_view mountpoint,
    const ExecutionCallbacks& callbacks) noexcept
    -> std::expected<void, std::string>;

}  // namespace cachyos::installer

#endif  // CACHYOS_INSTALLER_BOOTLOADER_HPP
