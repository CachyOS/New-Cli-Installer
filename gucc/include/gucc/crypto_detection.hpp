#ifndef CRYPTO_DETECTION_HPP
#define CRYPTO_DETECTION_HPP

#include "gucc/block_devices.hpp"

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::disk {

/// @brief Captures encryption state for a device or mountpoint.
struct CryptoDetection {
    bool is_luks{false};
    bool is_lvm{false};

    std::string luks_mapper_name{};
    // cryptdevice=UUID=xxxx:cryptroot
    std::string luks_dev{};
    std::string luks_uuid{};
};

/// @brief Detects crypto state for a given device.
/// @param devices A vector of BlockDevice objects.
/// @param device_name The device name to inspect.
/// @return CryptoDetection result, or nullopt if device not found.
auto detect_crypto_for_device(const std::vector<BlockDevice>& devices, std::string_view device_name) noexcept -> std::optional<CryptoDetection>;

/// @brief Detects crypto state for the device mounted at a given mountpoint.
/// @param devices A vector of BlockDevice objects.
/// @param mountpoint The mountpoint to look up.
/// @return CryptoDetection result, or nullopt if no device at mountpoint.
auto detect_crypto_for_mountpoint(const std::vector<BlockDevice>& devices, std::string_view mountpoint) noexcept -> std::optional<CryptoDetection>;

/// @brief Detects crypto state for a boot partition.
/// @param devices A vector of BlockDevice objects.
/// @param boot_mountpoint The boot mountpoint.
/// @return CryptoDetection result, or nullopt if no boot device found.
auto detect_crypto_for_boot(const std::vector<BlockDevice>& devices, std::string_view boot_mountpoint) noexcept -> std::optional<CryptoDetection>;

/// @brief Checks whether root and/or boot have crypt ancestry.
/// @param devices A vector of BlockDevice objects.
/// @param root_mountpoint The root mountpoint.
/// @param boot_mountpoint The boot mountpoint.
/// @return True if root or boot is encrypted.
auto is_encrypted(const std::vector<BlockDevice>& devices, std::string_view root_mountpoint, std::string_view boot_mountpoint) noexcept -> bool;

/// @brief Checks for full-disk encryption.
/// @param devices A vector of BlockDevice objects.
/// @param root_mountpoint The root mountpoint.
/// @param boot_mountpoint The boot mountpoint.
/// @param luks_flag Whether LUKS is already known to be active.
/// @return True if full-disk encryption is detected.
auto is_fde(const std::vector<BlockDevice>& devices, std::string_view root_mountpoint, std::string_view boot_mountpoint, bool luks_flag) noexcept -> bool;

/// @brief Returns device names mounted under a base mountpoint.
/// @param devices A vector of BlockDevice objects.
/// @param base_mountpoint The base mountpoint to filter by.
/// @return A vector of device name strings.
auto list_mounted_devices(const std::vector<BlockDevice>& devices, std::string_view base_mountpoint) noexcept -> std::vector<std::string>;

/// @brief Finds the underlying "part" ancestor for the device at a mountpoint.
/// @param devices A vector of BlockDevice objects.
/// @param mountpoint The mountpoint to look up.
/// @return The "part" type BlockDevice ancestor, or nullopt.
auto find_underlying_partition(const std::vector<BlockDevice>& devices, std::string_view mountpoint) noexcept -> std::optional<BlockDevice>;

}  // namespace gucc::disk

#endif  // CRYPTO_DETECTION_HPP
