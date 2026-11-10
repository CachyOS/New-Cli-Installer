#pragma once

#include "cachyos/installer_config.hpp"

// import gucc
#include "gucc/ranges_utils.hpp"

#include <string_view>  // for string_view

namespace cachyos::installer {

[[nodiscard]] inline auto partition_fs_name(const InstallerConfig& config, const PartitionConfig& part) noexcept -> std::string_view {
    if (part.fs_name.empty() && part.type == PartitionType::Root && config.fs_name) {
        return *config.fs_name;
    }
    return part.fs_name;
}

[[nodiscard]] inline auto root_fs_name(const InstallerConfig& config) noexcept -> std::string_view {
    if (const auto* root = gucc::utils::find_ptr(config.partitions, PartitionType::Root, &PartitionConfig::type)) {
        return partition_fs_name(config, *root);
    }
    return config.fs_name ? std::string_view{*config.fs_name} : std::string_view{};
}

}  // namespace cachyos::installer
