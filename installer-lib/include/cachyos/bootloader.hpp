#ifndef CACHYOS_INSTALLER_BOOTLOADER_HPP
#define CACHYOS_INSTALLER_BOOTLOADER_HPP

#include "cachyos/export.hpp"

#include "cachyos/types.hpp"

#include <expected>     // for expected
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

CACHYOS_INSTALLER_EXPORT_BEGIN

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

/// Runs argv inside the target chroot.
[[nodiscard]] auto arch_chroot(const std::vector<std::string>& argv, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string>;

}  // namespace cachyos::installer

CACHYOS_INSTALLER_EXPORT_END

#endif  // CACHYOS_INSTALLER_BOOTLOADER_HPP
