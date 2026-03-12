#ifndef CACHYOS_INSTALLER_CRYPTO_HPP
#define CACHYOS_INSTALLER_CRYPTO_HPP

#include "cachyos/types.hpp"

#include <expected>     // for expected
#include <string>       // for string
#include <string_view>  // for string_view

namespace cachyos::installer {

/// Detects LUKS/LVM state for the root device.
[[nodiscard]] auto detect_crypto_root(std::string_view mountpoint) noexcept
    -> std::expected<CryptoState, std::string>;

/// Detects LUKS state for the boot partition.
[[nodiscard]] auto detect_crypto_boot(std::string_view mountpoint, std::string_view uefi_mount) noexcept
    -> std::expected<CryptoState, std::string>;

/// Re-checks LUKS state after partitioning changes.
[[nodiscard]] auto recheck_luks(std::string_view mountpoint) noexcept
    -> std::expected<CryptoState, std::string>;

/// Adjusts initcpio/bootloader settings when /boot is encrypted.
[[nodiscard]] auto boot_encrypted_setting(std::string_view mountpoint, std::string_view uefi_mount) noexcept
    -> std::expected<void, std::string>;

/// Sets up a LUKS keyfile for automatic unlock.
[[nodiscard]] auto setup_luks_keyfile(std::string_view mountpoint) noexcept
    -> std::expected<void, std::string>;

}  // namespace cachyos::installer

#endif  // CACHYOS_INSTALLER_CRYPTO_HPP
