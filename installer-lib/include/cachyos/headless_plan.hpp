#pragma once

#include "cachyos/installer_config.hpp"
#include "cachyos/types.hpp"

#include <expected>  // for expected
#include <string>    // for string
#include <vector>    // for vector

namespace cachyos::installer {

/// The partition strategy to execute from headless config partition schema.
///
/// @param cfg The parsed configuration.
/// @param is_efi Whether the target boots via UEFI.
/// @return The strategy, or all validation errors found.
[[nodiscard]] auto headless_strategy_from_config(const InstallerConfig& cfg, bool is_efi) noexcept
    -> std::expected<PartitionStrategy, std::vector<std::string>>;

}  // namespace cachyos::installer
