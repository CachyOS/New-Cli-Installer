#include "cachyos/crypto.hpp"

#include <expected>     // for unexpected
#include <string>       // for string
#include <string_view>  // for string_view

#include <spdlog/spdlog.h>

namespace cachyos::installer {

auto detect_crypto_root(std::string_view /*mountpoint*/) noexcept
    -> std::expected<CryptoState, std::string> {
    return std::unexpected("not yet implemented");
}

auto detect_crypto_boot(std::string_view /*mountpoint*/, std::string_view /*uefi_mount*/) noexcept
    -> std::expected<CryptoState, std::string> {
    return std::unexpected("not yet implemented");
}

auto recheck_luks(std::string_view /*mountpoint*/) noexcept
    -> std::expected<CryptoState, std::string> {
    return std::unexpected("not yet implemented");
}

auto boot_encrypted_setting(std::string_view /*mountpoint*/, std::string_view /*uefi_mount*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto setup_luks_keyfile(std::string_view /*mountpoint*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

}  // namespace cachyos::installer
