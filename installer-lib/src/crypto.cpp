#include "cachyos/crypto.hpp"
#include "cachyos/disk.hpp"
#include "cachyos/result.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/crypto_detection.hpp"
#include "gucc/luks.hpp"
#include "gucc/string_utils.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for move
#include <vector>       // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

using cachyos::installer::block_devices;
using cachyos::installer::lift;

struct BootProbe final {
    std::vector<gucc::disk::BlockDevice> devices;
    std::string boot_mount;
};

auto probe_boot(std::string_view mountpoint, std::string_view uefi_mount) noexcept
    -> std::expected<BootProbe, std::string> {
    return block_devices().transform([&](std::vector<gucc::disk::BlockDevice>&& devices) {
        return BootProbe{
            .devices    = std::move(devices),
            .boot_mount = uefi_mount.empty() ? std::string{} : gucc::utils::target_path(mountpoint, uefi_mount),
        };
    });
}

auto setup_luks_keyfile(const std::vector<gucc::disk::BlockDevice>& devices, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    const auto& part_dev = gucc::disk::find_underlying_partition(devices, mountpoint);
    if (!part_dev) {
        return std::unexpected("failed to find underlying partition for root device");
    }

    const auto& partition = part_dev->name;
    const auto& slots     = gucc::crypto::luks_keyslot_count(partition);
    if (!slots) {
        spdlog::warn("Failed to count LUKS keyslots of {}: {}", partition, slots.error().context);
    } else if (*slots >= 4) {
        return {};
    }
    return lift(gucc::crypto::luks_setup_keyfile(gucc::utils::target_path(mountpoint, "/crypto_keyfile.bin"sv), mountpoint, partition, "--pbkdf-force-iterations 200000"));
}

}  // namespace

namespace cachyos::installer {

auto detect_crypto_root(std::string_view mountpoint) noexcept
    -> std::expected<CryptoState, std::string> {
    const auto& devices = block_devices();
    if (!devices) {
        return std::unexpected(devices.error());
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
    const auto& devices = block_devices();
    if (!devices) {
        return std::unexpected(devices.error());
    }

    const auto& crypto = gucc::disk::detect_crypto_for_boot(*devices, gucc::utils::target_path(mountpoint, uefi_mount));
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
    return probe_boot(mountpoint, uefi_mount).transform([mountpoint](const BootProbe& probe) {
        return gucc::disk::is_encrypted(probe.devices, mountpoint, probe.boot_mount);
    });
}

auto boot_encrypted_setting(std::string_view mountpoint, std::string_view uefi_mount, bool is_luks) noexcept
    -> std::expected<bool, std::string> {
    const auto& probe = probe_boot(mountpoint, uefi_mount);
    if (!probe) {
        return std::unexpected(probe.error());
    }
    if (!gucc::disk::is_fde(probe->devices, mountpoint, probe->boot_mount, is_luks)) {
        return false;
    }
    if (auto res = ::setup_luks_keyfile(probe->devices, mountpoint); !res) {
        return std::unexpected(fmt::format(FMT_COMPILE("failed to setup luks keyfile: {}"), res.error()));
    }
    return true;
}

auto setup_luks_keyfile(std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    const auto& devices = block_devices();
    if (!devices) {
        return std::unexpected(devices.error());
    }
    return ::setup_luks_keyfile(*devices, mountpoint);
}

}  // namespace cachyos::installer
