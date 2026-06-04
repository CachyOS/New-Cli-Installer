#pragma once

#include "cachyos/types.hpp"

// import gucc
#include "gucc/subprocess.hpp"

#include <cstdint>  // for int32_t

#include <expected>     // for expected
#include <stop_token>   // for stop_token
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

/// @file steps.hpp
/// @brief Per-step install entry points shared by the orchestrator and
/// frontends that want to drive individual install steps directly.
/// The orchestrator is a thin sequencer over these calls; any
/// caller can use them in isolation as long as preconditions are met.
namespace cachyos::installer::steps {

/// Per-line subprocess output callback.
using LogCallback = gucc::utils::SubProcess::LogLineCallback;

/// Unmount any existing partitions on the install target.
[[nodiscard]] auto umount(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Generate fstab inside the target.
[[nodiscard]] auto fstab(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Configure LUKS-encrypted swap when @ref InstallContext::encrypt_swap is set
/// and a swap device is known. No-op otherwise.
[[nodiscard]] auto encrypt_swap(const InstallContext& ctx) noexcept
    -> std::vector<std::string>;

/// Apply hostname/locale/keymap/timezone/hw_clock to the target system.
[[nodiscard]] auto system_settings(const SystemSettings& sys,
    const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Set the root password and create the primary user.
[[nodiscard]] auto users(const UserSettings& user,
    std::string_view root_password,
    const InstallContext& ctx,
    LogCallback log_cb,
    std::stop_token stop_token) noexcept
    -> std::vector<std::string>;

/// Intalls the base system into the target.
[[nodiscard]] auto base(const InstallContext& ctx,
    LogCallback log_cb,
    std::stop_token stop_token) noexcept
    -> std::expected<void, std::string>;

/// Generate machine id on the target.
[[nodiscard]] auto machine_id(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Install the selected desktop environment. No-op in server mode or when
/// no desktop is selected.
[[nodiscard]] auto desktop(const InstallContext& ctx,
    LogCallback log_cb,
    std::stop_token stop_token) noexcept
    -> std::expected<void, std::string>;

/// Install every chwd hardware-driver profile applicable to the target's
/// detected hardware.
[[nodiscard]] auto chwd(const InstallContext& ctx,
    LogCallback log_cb,
    std::stop_token stop_token) noexcept
    -> std::expected<void, std::string>;

/// Carry the live ISO's NetworkManager profiles into the target.
/// Returns the number of profiles copied; -1 on I/O failure.
auto network_carryover(const InstallContext& ctx) noexcept -> std::int32_t;

/// Install the configured bootloader.
[[nodiscard]] auto bootloader(const InstallContext& ctx,
    LogCallback log_cb,
    std::stop_token stop_token) noexcept
    -> std::expected<void, std::string>;

/// Probe the post-install crypto layout and update @p ctx.crypto in place.
auto detect_crypto(InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Enable systemd services inside the target.
[[nodiscard]] auto enable_services(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Run final consistency checks on the installed system. Both `errors` and
/// `warnings` of the returned `ValidationResult` are non-fatal per orchestrator
/// policy.
[[nodiscard]] auto final_validation(const InstallContext& ctx) noexcept
    -> ValidationResult;

/// Copy /tmp/cachyos-install.log into the target and unmount partitions.
[[nodiscard]] auto cleanup(const InstallContext& ctx) noexcept
    -> std::vector<std::string>;

}  // namespace cachyos::installer::steps
