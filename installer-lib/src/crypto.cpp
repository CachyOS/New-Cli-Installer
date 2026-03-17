#include "cachyos/crypto.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/crypto_detection.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/luks.hpp"

#include <charconv>     // for from_chars
#include <string>       // for string
#include <string_view>  // for string_view

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {
template <typename T = std::int32_t>
    requires std::numeric_limits<T>::is_integer
inline T to_int(const std::string_view& str) {
    T result = 0;
    std::from_chars(str.data(), str.data() + str.size(), result);
    return result;
}
}  // namespace

namespace cachyos::installer {

auto detect_crypto_root(std::string_view mountpoint) noexcept
    -> std::expected<CryptoState, std::string> {
    const auto& devices = gucc::disk::list_block_devices();
    if (!devices) {
        return std::unexpected("failed to find block devices");
    }

    const auto& crypto = gucc::disk::detect_crypto_for_mountpoint(*devices, mountpoint);
    if (!crypto) {
        return std::unexpected("failed to find root device on mountpoint");
    }

    CryptoState state{};
    if (!crypto->is_luks) {
        spdlog::info("crypt type not found for root");
        return state;
    }

    state.is_luks        = true;
    state.luks_root_name = crypto->luks_mapper_name;
    state.luks_dev       = crypto->luks_dev;
    state.is_lvm         = crypto->is_lvm;

    if (!crypto->luks_uuid.empty()) {
        state.luks_uuid = crypto->luks_uuid;
    }

    return state;
}

auto detect_crypto_boot(std::string_view mountpoint, std::string_view uefi_mount) noexcept
    -> std::expected<CryptoState, std::string> {
    const auto& devices = gucc::disk::list_block_devices();
    if (!devices) {
        return std::unexpected("failed to find block devices");
    }

    const auto boot_mount = fmt::format(FMT_COMPILE("{}{}"), mountpoint, uefi_mount);
    const auto& crypto    = gucc::disk::detect_crypto_for_boot(*devices, boot_mount);
    if (!crypto) {
        return std::unexpected("failed to find boot device on mountpoint");
    }

    CryptoState state{};
    if (!crypto->is_luks) {
        spdlog::info("crypt type not found for boot");
        return state;
    }
    if (crypto->luks_uuid.empty()) {
        return std::unexpected("failed to get uuid of cryptboot");
    }

    state.is_luks   = true;
    state.is_lvm    = crypto->is_lvm;
    state.luks_dev  = crypto->luks_dev;
    state.luks_uuid = crypto->luks_uuid;

    return state;
}

auto recheck_luks(std::string_view mountpoint, std::string_view uefi_mount) noexcept
    -> std::expected<bool, std::string> {
    const auto& devices = gucc::disk::list_block_devices();
    if (!devices) {
        return std::unexpected("failed to find block devices");
    }

    const auto boot_mount = !uefi_mount.empty()
        ? fmt::format(FMT_COMPILE("{}{}"), mountpoint, uefi_mount)
        : std::string{};

    return gucc::disk::is_encrypted(*devices, mountpoint, boot_mount);
}

auto boot_encrypted_setting(std::string_view mountpoint, std::string_view uefi_mount, bool is_luks) noexcept
    -> std::expected<bool, std::string> {
    const auto& devices = gucc::disk::list_block_devices();
    if (!devices) {
        return std::unexpected("failed to find block devices");
    }

    const auto boot_mount = !uefi_mount.empty()
        ? fmt::format(FMT_COMPILE("{}{}"), mountpoint, uefi_mount)
        : std::string{};

    if (gucc::disk::is_fde(*devices, mountpoint, boot_mount, is_luks)) {
        auto result = setup_luks_keyfile(mountpoint);
        if (!result) {
            return std::unexpected(fmt::format("failed to setup luks keyfile: {}", result.error()));
        }
        return true;
    }
    return false;
}

auto setup_luks_keyfile(std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    const auto& devices = gucc::disk::list_block_devices();
    if (!devices) {
        return std::unexpected("failed to find block devices");
    }

    const auto& part_dev = gucc::disk::find_underlying_partition(*devices, mountpoint);
    if (!part_dev) {
        return std::unexpected("failed to find underlying partition for root device");
    }

    const auto& partition    = part_dev->name;
    const auto& lukskeys_str = gucc::utils::exec(
        fmt::format(FMT_COMPILE("cryptsetup luksDump '{}' | grep 'ENABLED' | wc -l"), partition));
    if (to_int(lukskeys_str) < 4) {
        const auto keyfile_path = fmt::format(FMT_COMPILE("{}/crypto_keyfile.bin"), mountpoint);
        if (!gucc::crypto::luks1_setup_keyfile(keyfile_path, mountpoint, partition, "--pbkdf-force-iterations 200000")) {
            return std::unexpected("failed to setup luks1 keyfile");
        }
    }
    return {};
}

}  // namespace cachyos::installer
