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

/// Re-checks whether LUKS is in use after partitioning changes.
[[nodiscard]] auto recheck_luks(std::string_view mountpoint, std::string_view uefi_mount) noexcept
    -> std::expected<bool, std::string>;

/// Checks for full-disk encryption and sets up LUKS keyfile if needed.
/// @param is_luks Whether LUKS is already known to be active.
/// @return true if FDE was detected.
[[nodiscard]] auto boot_encrypted_setting(std::string_view mountpoint, std::string_view uefi_mount, bool is_luks) noexcept
    -> std::expected<bool, std::string>;

/// Sets up a LUKS keyfile for automatic unlock.
[[nodiscard]] auto setup_luks_keyfile(std::string_view mountpoint) noexcept
    -> std::expected<void, std::string>;

}  // namespace cachyos::installer

#endif  // CACHYOS_INSTALLER_CRYPTO_HPP
