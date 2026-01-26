#ifndef PARTITION_CONFIG_HPP
#define PARTITION_CONFIG_HPP

#include <cstdint>      // for uint8_t
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::fs {

/// @brief Supported filesystem types for partitioning
enum class FilesystemType : std::uint8_t {
    Btrfs,
    Ext4,
    F2fs,
    Xfs,
    Vfat,
    LinuxSwap,
    Zfs,
    Unknown
};

/// @brief Represents a mount option with its name and description
struct MountOption final {
    std::string name;
    std::string description;

    constexpr bool operator==(const MountOption&) const = default;
};

/// @brief Configuration for generating default partition schema
struct DefaultPartitionSchemaConfig final {
    /// Root filesystem type (default: btrfs)
    FilesystemType root_fs_type{FilesystemType::Btrfs};

    /// EFI partition size (default: 2GiB), used only for UEFI systems
    std::string efi_partition_size{"2GiB"};

    /// Swap partition size (default: empty means no swap partition)
    std::optional<std::string> swap_partition_size{};

    /// Boot partition size for BIOS systems (default: empty means no separate boot)
    std::optional<std::string> boot_partition_size{};

    /// Whether the target device is an SSD (affects default mount options)
    bool is_ssd{false};

    /// Custom mount options for root partition (if empty, defaults will be used)
    std::optional<std::string> root_mount_opts{};

    /// EFI/Boot mountpoint (default: /boot)
    std::string boot_mountpoint{"/boot"};

    /// Whether to create btrfs subvolumes (only applies when root_fs_type is Btrfs)
    bool create_btrfs_subvolumes{true};

    constexpr bool operator==(const DefaultPartitionSchemaConfig&) const = default;
};

/// @brief Convert FilesystemType enum to string representation
/// @param fs_type The filesystem type to convert
/// @return String representation of the filesystem type
auto filesystem_type_to_string(FilesystemType fs_type) noexcept -> std::string_view;

/// @brief Convert string to FilesystemType enum
/// @param fs_name The filesystem name string
/// @return FilesystemType enum value (Unknown if not recognized)
auto string_to_filesystem_type(std::string_view fs_name) noexcept -> FilesystemType;

/// @brief Get available mount options for a filesystem type
/// @param fs_type The filesystem type
/// @return Vector of available mount options
auto get_available_mount_opts(FilesystemType fs_type) noexcept -> std::vector<MountOption>;

/// @brief Get default mount options for a filesystem type based on device type
/// @param fs_type The filesystem type
/// @param is_ssd Whether the target device is an SSD
/// @return Comma-separated string of default mount options
auto get_default_mount_opts(FilesystemType fs_type, bool is_ssd) noexcept -> std::string;

/// @brief Get the mkfs command for a filesystem type
/// @param fs_type The filesystem type
/// @return The mkfs command string (e.g., "mkfs.btrfs -f")
auto get_mkfs_command(FilesystemType fs_type) noexcept -> std::string_view;

/// @brief Get the sfdisk partition type alias for a filesystem
/// @param fs_type The filesystem type
/// @return The sfdisk type alias (L=Linux, U=UEFI, S=Swap)
auto get_sfdisk_type_alias(FilesystemType fs_type) noexcept -> std::string_view;

/// @brief Convert internal filesystem name to fstab-compatible name
/// @param fs_type The filesystem type
/// @return The fstab-compatible filesystem name
auto get_fstab_fs_name(FilesystemType fs_type) noexcept -> std::string_view;

}  // namespace gucc::fs

#endif  // PARTITION_CONFIG_HPP
