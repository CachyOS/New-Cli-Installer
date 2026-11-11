#pragma once

#include "gucc/export.hpp"

#include "gucc/error.hpp"
#include "gucc/partition_config.hpp"

#include <string_view>  // for string_view

GUCC_EXPORT_BEGIN

namespace gucc::initcpio {

struct InitcpioConfig {
    gucc::fs::FilesystemType filesystem_type{gucc::fs::FilesystemType::Ext4};
    bool is_lvm{false};
    bool is_luks{false};
    bool has_swap{false};
    bool has_plymouth{false};
    bool use_systemd_hook{false};
    bool is_btrfs_multi_device{false};
    // native zfs encryption on the root pool
    bool is_zfs_encrypted{false};
};

/// Configure initramfs for the given filesystem/encryption setup.
[[nodiscard]] auto setup_initcpio_config(std::string_view initcpio_path, const InitcpioConfig& config) noexcept -> Result<void>;

/// Installs hook that skips initramfs hook on root.
[[nodiscard]] auto install_deferral_hook(std::string_view root) noexcept -> Result<void>;

/// Uninstalls postpone initramfs hook.
[[nodiscard]] auto remove_deferral_hook(std::string_view root) noexcept -> Result<void>;

}  // namespace gucc::initcpio

GUCC_EXPORT_END
