#ifndef ORCHESTRATOR_HPP
#define ORCHESTRATOR_HPP

#include "cachyos/types.hpp"

#include <optional>     // for optional
#include <stop_token>   // for stop_token
#include <string_view>  // for string_view

namespace cachyos::installer {

/// Parses a pacman-style "(  N/M)" progress prefix from a log line.
[[nodiscard]] auto parse_pacman_progress(std::string_view line) noexcept -> std::optional<double>;

/// Runs the full install sequence from a populated context.
///
/// Mutates @p ctx as the install progresses: partition_schema and swap_device
/// are filled from the mount step, and crypto state is filled from the
/// post-install detection step.
///
/// Progress is reported through @p callbacks if set; log output from
/// subprocesses is forwarded.
///
/// Pass a stop token from a `std::stop_source` to support cancellation:
/// cancel checks run between every step, and any subprocess in flight at the
/// time `request_stop()` is called receives SIGTERM.
[[nodiscard]] auto run(InstallContext& ctx,
    const SystemSettings& sys,
    const UserSettings& user,
    std::string_view root_password,
    const ExecutionCallbacks& callbacks,
    std::stop_token stop_token = {}) noexcept -> ValidationResult;

}  // namespace cachyos::installer

#endif  // ORCHESTRATOR_HPP
