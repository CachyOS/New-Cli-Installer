#pragma once

#include "cachyos/export.hpp"

#include "cachyos/installer_config.hpp"
#include "cachyos/session.hpp"
#include "cachyos/types.hpp"

CACHYOS_INSTALLER_EXPORT_BEGIN

namespace cachyos::installer {

[[nodiscard]] auto run_from_config(const InstallerConfig& config, InstallSession& session) noexcept -> ValidationResult;

/// Process exit status for a headless run: 0 when every step went through,
/// 1 when the install failed, 2 when it finished but a step reported a warning.
[[nodiscard]] auto headless_exit_code(const ValidationResult& result) noexcept -> int;

}  // namespace cachyos::installer

CACHYOS_INSTALLER_EXPORT_END
