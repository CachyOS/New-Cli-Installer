#ifndef INITCPIO_HPP
#define INITCPIO_HPP

#include "gucc/error.hpp"
#include "gucc/partition_config.hpp"

#include <string_view>  // for string_view

namespace gucc::initcpio {

struct InitcpioConfig {
    gucc::fs::FilesystemType filesystem_type{gucc::fs::FilesystemType::Ext4};
    bool is_lvm{false};
    bool is_luks{false};
    bool has_swap{false};
    bool has_plymouth{false};
    bool use_systemd_hook{false};
    bool is_btrfs_multi_device{false};
};

// Configure mkinitcpio.conf for the given filesystem/encryption setup.
// Builds hooks list from scratch matching calamares initcpiocfg logic.
[[nodiscard]] auto setup_initcpio_config(std::string_view initcpio_path, const InitcpioConfig& config) noexcept -> Result<void>;

}  // namespace gucc::initcpio

#endif  // INITCPIO_HPP
