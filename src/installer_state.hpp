#pragma once

#include "cachyos/config.hpp"
#include "cachyos/installer_config.hpp"

// import gucc
#include "gucc/bootloader.hpp"
#include "gucc/btrfs.hpp"

#include <cstdint>  // for uint8_t

#include <optional>  // for optional
#include <string>    // for string
#include <vector>    // for vector

namespace tui {

/// Partition picking in the menus.
struct DiskSelection {
    /// "<device> <size>" rows for TUI.
    std::vector<std::string> partitions;
    /// Device the current menu operates on.
    std::string partition;
    /// Mountpoint of the additional partition being mounted.
    std::string mount;

    std::string mkfs_command;
    std::string mount_opts;

    std::string zfs_zpool_name{"zpcachyos"};

    std::vector<gucc::fs::BtrfsSubvolume> btrfs_subvolumes;
    bool use_default_subvolumes{true};

    /// Provided layout from config.
    std::vector<cachyos::installer::PartitionConfig> ready_partitions;
};

/// settings "menus". frontend mode.
enum class MenuMode : std::uint8_t {
    Ask,
    Simple,
    Advanced,
};

struct InstallerState {
    cachyos::installer::InstallerInputs inputs;
    std::optional<gucc::bootloader::BootloaderType> bootloader;
    DiskSelection disk;

    std::string post_install;
    MenuMode menus{MenuMode::Ask};
    /// Progress widgets print plain lines instead.
    bool plain_progress{false};
};

/// Parses config into the state
void init_state(const cachyos::installer::InstallerConfig& config) noexcept;

[[nodiscard]] auto state() noexcept -> InstallerState&;

}  // namespace tui
