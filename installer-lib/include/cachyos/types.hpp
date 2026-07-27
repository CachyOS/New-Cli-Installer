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
#include <variant>      // for variant
#include <vector>       // for vector

namespace cachyos::installer {

// Default net-profile source URLs.
inline constexpr std::string_view kDefaultNetProfilesUrl{"https://raw.githubusercontent.com/CachyOS/New-Cli-Installer/master/net-profiles.toml"};
inline constexpr std::string_view kDefaultNetProfilesFallbackUrl{"file:///var/lib/cachyos-installer/net-profiles.toml"};

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

/// How the target disk gets prepared.
namespace partition_strategy {

    /// Partitions are already formatted and mounted by the caller.
    struct UseExisting final { };

    /// Partitions already exist. Formats and mounts
    /// without touching partition table.
    struct ApplyLayout final {
        MountSelections selections;
    };

    /// Create @p partitions on @p device, then format and mount them.
    /// @warning erases the partition table on @p device.
    struct CreateLayout final {
        std::string device;
        std::vector<gucc::fs::Partition> partitions;
        std::vector<gucc::fs::BtrfsSubvolume> btrfs_subvolumes;
    };

    /// Let the installer pick a default layout for @p device.
    /// @warning erases everything on @p device.
    struct EraseAndAuto final {
        std::string device;
    };

}  // namespace partition_strategy

using PartitionStrategy = std::variant<
    partition_strategy::UseExisting,
    partition_strategy::ApplyLayout,
    partition_strategy::CreateLayout,
    partition_strategy::EraseAndAuto>;

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

    /// How the Partition step prepares the target disk.
    PartitionStrategy strategy{partition_strategy::UseExisting{}};

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

    /// When true, run `chwd -a` against the target after the desktop step
    /// to install every hardware-driver profile applicable to the detected
    /// hardware.
    bool install_chwd_profiles{false};

    // Network profiles
    std::string net_profiles_url{kDefaultNetProfilesUrl};
    std::string net_profiles_fallback_url{kDefaultNetProfilesFallbackUrl};
    std::string net_profiles_user_path;

    /// Carry the live ISO's NetworkManager system-connections into the target.
    bool carry_live_network{true};
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

using ProgressCallback = std::function<void(const ProgressEvent&)>;

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
    /// Supplementary groups added to the new user. Empty falls back to the
    /// CachyOS default; pass an explicit list to override.
    std::vector<std::string> groups;
    bool autologin{};
};

}  // namespace cachyos::installer

#endif  // CACHYOS_INSTALLER_TYPES_HPP
