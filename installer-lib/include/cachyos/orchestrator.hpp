#ifndef ORCHESTRATOR_HPP
#define ORCHESTRATOR_HPP

#include "cachyos/types.hpp"

#include <string_view>  // for string_view

namespace cachyos::installer {

/// Runs the full install sequence from a populated context.
///
/// Mutates @p ctx as the install progresses: partition_schema and swap_device
/// are filled from the mount step, and crypto state is filled from the
/// post-install detection step.
///
/// Progress is reported through @p callbacks if set; log output from
/// subprocesses is forwarded.
[[nodiscard]] auto run(InstallContext& ctx,
    const SystemSettings& sys,
    const UserSettings& user,
    std::string_view root_password,
    const ExecutionCallbacks& callbacks) noexcept -> ValidationResult;

}  // namespace cachyos::installer

#endif  // ORCHESTRATOR_HPP
