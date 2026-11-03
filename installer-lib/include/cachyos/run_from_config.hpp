#pragma once

#include "cachyos/export.hpp"

#include "cachyos/installer_config.hpp"
#include "cachyos/session.hpp"
#include "cachyos/types.hpp"

CACHYOS_INSTALLER_EXPORT_BEGIN

namespace cachyos::installer {

[[nodiscard]] auto run_from_config(const InstallerConfig& config, InstallSession& session) noexcept -> ValidationResult;

}  // namespace cachyos::installer

CACHYOS_INSTALLER_EXPORT_END
