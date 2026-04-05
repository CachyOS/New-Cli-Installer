#include "gucc/crypto_detection.hpp"
#include "gucc/block_devices.hpp"

#include <algorithm>  // for any_of
#include <ranges>     // for ranges::*

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::disk {

auto detect_crypto_for_device(const std::vector<BlockDevice>& devices, std::string_view device_name) noexcept -> std::optional<CryptoDetection> {
    const auto& device = find_device_by_name(devices, device_name);
    if (!device) {
        spdlog::error("Failed to find device with name: {}", device_name);
        return std::nullopt;
    }

    // Check if device has crypt in ancestry (or is itself crypt type)
    const auto& crypt_dev = find_ancestor_of_type(devices, device->name, "crypt"sv);
    if (!crypt_dev) {
        return CryptoDetection{};
    }

    CryptoDetection result{};
    result.is_luks          = true;
    result.luks_mapper_name = std::string{strip_device_prefix(crypt_dev->name)};

    // Check LUKS-on-LVM: lvm device with crypto_LUKS fstype
    const auto& luks_on_lvm = find_devices_by_type_and_fstype(devices, "lvm"sv, "crypto_LUKS"sv);
    if (!luks_on_lvm.empty()) {
        result.is_lvm = true;
        for (const auto& cryptpart : luks_on_lvm) {
            result.luks_dev = fmt::format(FMT_COMPILE("cryptdevice={}:{}"), cryptpart.name, result.luks_mapper_name);
        }
        return result;
    }

    // Check LVM-on-LUKS: crypt device with LVM2_member fstype
    const auto& lvm_on_luks = find_devices_by_type_and_fstype(devices, "crypt"sv, "LVM2_member"sv);
    if (!lvm_on_luks.empty()) {
        result.is_lvm = true;
        // Find the underlying LUKS partition's UUID by walking ancestors
        const auto& part_ancestor = find_ancestor_of_type(devices, lvm_on_luks.front().name, "part"sv);
        if (part_ancestor) {
            result.luks_uuid = part_ancestor->uuid;
            result.luks_dev  = fmt::format(FMT_COMPILE("cryptdevice=UUID={}:{}"), result.luks_uuid, result.luks_mapper_name);
        }
        return result;
    }

    // Check LUKS alone: part device with crypto_LUKS fstype
    const auto& luks_parts = find_devices_by_type_and_fstype(devices, "part"sv, "crypto_LUKS"sv);
    if (!luks_parts.empty()) {
        result.luks_uuid = luks_parts.front().uuid;
        result.luks_dev  = fmt::format(FMT_COMPILE("cryptdevice=UUID={}:{}"), result.luks_uuid, result.luks_mapper_name);
    }

    return result;
}

auto detect_crypto_for_mountpoint(const std::vector<BlockDevice>& devices, std::string_view mountpoint) noexcept -> std::optional<CryptoDetection> {
    const auto& mnt_dev = find_device_by_mountpoint(devices, mountpoint);
    if (!mnt_dev) {
        spdlog::error("Failed to find device by mountpoint: {}", mountpoint);
        return std::nullopt;
    }
    return detect_crypto_for_device(devices, mnt_dev->name);
}

auto detect_crypto_for_boot(const std::vector<BlockDevice>& devices, std::string_view boot_mountpoint) noexcept -> std::optional<CryptoDetection> {
    const auto& boot_dev = find_device_by_mountpoint(devices, boot_mountpoint);
    if (!boot_dev) {
        spdlog::error("Failed to find boot device by mountpoint: {}", boot_mountpoint);
        return std::nullopt;
    }

    // Find crypt device in boot's ancestry
    const auto& crypt_dev = find_ancestor_of_type(devices, boot_dev->name, "crypt"sv);
    if (!crypt_dev) {
        return CryptoDetection{};
    }

    CryptoDetection result{};
    result.is_luks          = true;
    result.luks_mapper_name = std::string{strip_device_prefix(crypt_dev->name)};

    // Check if LVM on LUKS (boot device is on lvm)
    if (boot_dev->type == "lvm"sv) {
        result.is_lvm = true;
    }

    // Get UUID of the underlying partition
    const auto& part_ancestor = find_ancestor_of_type(devices, crypt_dev->name, "part"sv);
    if (part_ancestor) {
        result.luks_uuid = part_ancestor->uuid;
        result.luks_dev  = fmt::format(FMT_COMPILE("cryptdevice=UUID={}:{}"), result.luks_uuid, result.luks_mapper_name);
    }

    return result;
}

auto is_encrypted(const std::vector<BlockDevice>& devices, std::string_view root_mountpoint, std::string_view boot_mountpoint) noexcept -> bool {
    const auto& root_dev      = find_device_by_mountpoint(devices, root_mountpoint);
    const bool root_encrypted = root_dev && has_type_in_ancestry(devices, root_dev->name, "crypt"sv);

    if (!boot_mountpoint.empty()) {
        const auto& boot_dev      = find_device_by_mountpoint(devices, boot_mountpoint);
        const bool boot_encrypted = boot_dev && has_type_in_ancestry(devices, boot_dev->name, "crypt"sv);
        return root_encrypted || boot_encrypted;
    }
    return root_encrypted;
}

auto is_fde(const std::vector<BlockDevice>& devices, std::string_view root_mountpoint, std::string_view boot_mountpoint, bool luks_flag) noexcept -> bool {
    if (!boot_mountpoint.empty()) {
        // Separate boot partition: FDE if boot is encrypted
        const auto& boot_dev = find_device_by_mountpoint(devices, boot_mountpoint);
        return boot_dev && has_type_in_ancestry(devices, boot_dev->name, "crypt"sv);
    }
    // No separate boot: FDE if root is encrypted (or LUKS flag is set)
    const auto& root_dev = find_device_by_mountpoint(devices, root_mountpoint);
    return luks_flag || (root_dev && has_type_in_ancestry(devices, root_dev->name, "crypt"sv));
}

auto list_mounted_devices(const std::vector<BlockDevice>& devices, std::string_view base_mountpoint) noexcept -> std::vector<std::string> {
    auto filter_pred = [&base_mountpoint](const auto& mp) { return mp.starts_with(base_mountpoint); };
    std::vector<std::string> result{};
    for (const auto& dev : devices) {
        if (std::ranges::any_of(dev.mountpoints, filter_pred)) {
            result.emplace_back(dev.name);
        }
    }
    return result;
}

auto find_underlying_partition(const std::vector<BlockDevice>& devices, std::string_view mountpoint) noexcept -> std::optional<BlockDevice> {
    const auto& mnt_dev = find_device_by_mountpoint(devices, mountpoint);
    if (!mnt_dev) {
        spdlog::error("Failed to find device by mountpoint: {}", mountpoint);
        return std::nullopt;
    }
    return find_ancestor_of_type(devices, mnt_dev->name, "part"sv);
}

}  // namespace gucc::disk
