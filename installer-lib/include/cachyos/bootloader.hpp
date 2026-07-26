#ifndef CACHYOS_INSTALLER_BOOTLOADER_HPP
#define CACHYOS_INSTALLER_BOOTLOADER_HPP

#include "cachyos/types.hpp"

#include <expected>     // for expected
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace cachyos::installer {

/// Installs the bootloader selected in the context.
[[nodiscard]] auto install_bootloader(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Installs GRUB in UEFI mode.
[[nodiscard]] auto install_grub_uefi(const InstallContext& ctx, std::string_view bootid,
    bool as_default) noexcept
    -> std::expected<void, std::string>;

/// Installs GRUB in BIOS mode.
[[nodiscard]] auto install_grub_bios(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Installs rEFInd bootloader.
[[nodiscard]] auto install_refind(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Installs systemd-boot.
[[nodiscard]] auto install_systemd_boot(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Installs Limine bootloader.
[[nodiscard]] auto install_limine(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Builds the kernel command-line parameters string.
[[nodiscard]] auto get_kernel_params(const InstallContext& ctx) noexcept
    -> std::expected<std::vector<std::string>, std::string>;

/// Runs grub-mkconfig inside the chroot.
[[nodiscard]] auto grub_mkconfig(std::string_view mountpoint) noexcept
    -> std::expected<void, std::string>;

/// Runs a command inside the target chroot.
[[nodiscard]] auto arch_chroot(std::string_view command, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string>;

}  // namespace cachyos::installer

#endif  // CACHYOS_INSTALLER_BOOTLOADER_HPP
