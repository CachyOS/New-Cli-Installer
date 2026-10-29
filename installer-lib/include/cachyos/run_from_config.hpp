#pragma once

#include "cachyos/installer_config.hpp"
#include "cachyos/session.hpp"
#include "cachyos/types.hpp"

namespace cachyos::installer {

[[nodiscard]] auto run_from_config(const InstallerConfig& config, InstallSession& session) noexcept -> ValidationResult;

}  // namespace cachyos::installer
