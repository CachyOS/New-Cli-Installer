#ifndef CACHYOS_INSTALLER_TYPES_HPP
#define CACHYOS_INSTALLER_TYPES_HPP

// import gucc
#include "gucc/bootloader.hpp"
#include "gucc/btrfs.hpp"
#include "gucc/partition.hpp"

#include <cstdint>      // for int32_t
#include <functional>   // for function
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace cachyos::installer {

/// User-supplied root-partition choice consumed by the mount step.
struct RootPartitionSelection {
    std::string device;
    std::string fstype;
    std::string mkfs_command;
    std::string mount_opts;
    bool format_requested{};
};

/// User-supplied swap choice (none, swapfile, or partition).
struct SwapSelection {
    enum class Type : std::uint8_t {
        None,
        Swapfile,
        Partition
    };

    Type type{Type::None};
    std::string device;
    std::string swapfile_size;
    bool needs_mkswap{};
};

/// User-supplied EFI System Partition choice.
struct EspSelection {
    std::string device;
    std::string mountpoint;
    bool format_requested{};
};

/// User-supplied additional partition (e.g. /home, /var).
struct AdditionalPartSelection {
    std::string device;
    std::string mountpoint;
    std::string fstype;
    std::string mkfs_command;
    std::string mount_opts;
    bool format_requested{};
};

/// Full set of partition/mount choices, produced by a frontend planner and
/// consumed by the orchestrator's `Partition` step.
struct MountSelections {
    RootPartitionSelection root;
    SwapSelection swap;
    EspSelection esp;
    std::vector<AdditionalPartSelection> additional;
    std::vector<gucc::fs::BtrfsSubvolume> btrfs_subvolumes;
};

/// Crypto/LUKS/LVM state detected from the system.
struct CryptoState {
    bool is_luks{};
    bool is_lvm{};
    bool is_fde{};

    std::string luks_dev;
    std::string luks_name;
    std::string luks_uuid;
    std::string luks_root_name;

    std::int32_t lvm_sep_boot{0};
};

/// Contains explicit typed params instead of reading global state.
struct InstallContext {
    /// Modes to define installer boot process.
    enum class SystemMode : std::uint8_t {
        UEFI,
        BIOS,
    };

    // Mounting
    std::string mountpoint{"/mnt"};
    std::string device;

    // System
    SystemMode system_mode{SystemMode::UEFI};
    bool hostcache{true};

    // Partitions
    std::vector<gucc::fs::Partition> partition_schema;
    std::string swap_device;
    std::string uefi_mount;
    std::vector<std::string> zfs_zpool_names;

    /// When set, the orchestrator's Partition step uses these selections as-is
    /// and skips `auto_partition`.
    std::optional<MountSelections> mount_selections;

    bool encrypt_swap{false};

    // Bootloader
    gucc::bootloader::BootloaderType bootloader{gucc::bootloader::BootloaderType::Grub};

    // Crypto
    CryptoState crypto;

    // Installation
    std::string kernel;
    std::string desktop;
    std::string filesystem_name;
    std::string keymap{"us"};
    bool server_mode{};

    // Network profiles
    std::string net_profiles_url{"https://raw.githubusercontent.com/CachyOS/New-Cli-Installer/master/net-profiles.toml"};
    std::string net_profiles_fallback_url{"file:///var/lib/cachyos-installer/net-profiles.toml"};
};

/// Progress event types reported by library operations.
enum class ProgressEventType : std::uint8_t {
    Started,
    Running,
    Completed,
    Failed,
    Cancelled,
};

/// A progress event emitted during long-running operations.
struct ProgressEvent {
    ProgressEventType type{ProgressEventType::Running};
    std::string message;
    /// 0.0 to 1.0; negative means indeterminate
    double fraction{-1.0};
};

/// Callbacks for frontend integration.
using ProgressCallback = std::function<void(const ProgressEvent&)>;
using LogLineCallback  = std::function<void(std::string_view line)>;

struct ExecutionCallbacks {
    ProgressCallback on_progress;
    LogLineCallback on_log_line;
};

/// Validation result from final_check and similar operations.
struct ValidationResult {
    bool success{};
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

/// System information detected at startup.
struct SystemInfo {
    InstallContext::SystemMode system_mode{InstallContext::SystemMode::UEFI};
};

/// System-level settings to apply to the installed system.
struct SystemSettings {
    /// Modes for chrony/timed.
    enum class HwClock : std::uint8_t {
        UTC,
        Localtime,
    };
    std::string hostname;
    std::string locale;
    std::string xkbmap;
    std::string keymap;
    std::string timezone;
    std::optional<HwClock> hw_clock{};
};

/// User account settings.
struct UserSettings {
    std::string username;
    std::string password;
    std::string shell;
};

}  // namespace cachyos::installer

#endif  // CACHYOS_INSTALLER_TYPES_HPP
