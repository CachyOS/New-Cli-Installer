#pragma once

#include "cachyos/config.hpp"
#include "cachyos/installer_config.hpp"

// import gucc
#include "gucc/bootloader.hpp"
#include "gucc/btrfs.hpp"
#include "gucc/zfs_types.hpp"

#include <cstdint>  // for int32_t

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace tui {

/// Partition picking in the menus.
struct DiskSelection {
    /// "<device> <size>" rows for TUI.
    std::vector<std::string> partitions;
    /// Device the current menu operates on.
    std::string partition;
    std::string root_part;
    /// Mountpoint of the additional partition being mounted.
    std::string mount;
    std::string uefi_part;

    std::string mkfs_command;
    std::vector<std::string> fs_opts;
    std::string mount_opts;

    std::string zfs_zpool_name{"zpcachyos"};
    gucc::fs::ZfsSetupConfig zfs_setup;

    std::vector<gucc::fs::BtrfsSubvolume> btrfs_subvolumes;
    bool use_default_subvolumes{true};

    /// Provided layout from config.
    std::vector<cachyos::installer::PartitionConfig> ready_partitions;
};

struct InstallerState {
    cachyos::installer::InstallerInputs inputs;
    std::optional<gucc::bootloader::BootloaderType> bootloader;
    DiskSelection disk;

    std::string luks_password;
    std::string post_install;
    /// TUI menu mode: -1 selection, 1 simple, 2 advanced.
    std::int32_t menus{-1};
    /// Progress widgets print plain lines instead.
    bool plain_progress{false};
};

/// Parses config into the state
void init_state(const cachyos::installer::InstallerConfig& config) noexcept;

[[nodiscard]] auto state() noexcept -> InstallerState&;

[[nodiscard]] inline auto system_name() noexcept -> std::string_view {
    return state().inputs.ctx.is_uefi() ? "UEFI" : "BIOS";
}

}  // namespace tui
