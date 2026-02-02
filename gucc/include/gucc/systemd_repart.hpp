#ifndef SYSTEMD_REPART_HPP
#define SYSTEMD_REPART_HPP

#include "gucc/partition.hpp"
#include "gucc/partition_config.hpp"

#include <cstdint>      // for uint8_t
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::disk {

/// @brief Encryption mode
enum class RepartEncryptMode : std::uint8_t {
    /// No encryption
    Off,
    /// LUKS encryption with key file
    KeyFile,
    /// LUKS encryption with TPM2
    Tpm2,
    /// LUKS encryption with both key file and TPM2
    KeyFilePlusTpm2
};

/// @brief Empty mode
enum class RepartEmptyMode : std::uint8_t {
    /// Refuse to operate on empty disk (default)
    Refuse,
    /// Allow operating on empty or existing disk
    Allow,
    /// Require empty disk, refuse if partition table exists
    Require,
    /// Force fresh partition table, erasing all data
    Force,
    /// Create new loopback file
    Create
};

/// @brief Configuration for a single repart.d partition definition
struct RepartPartitionConfig final {
    /// GPT partition type (e.g., "esp", "swap", "root", "home", "linux-generic")
    std::string type{};

    /// Partition label (optional)
    std::optional<std::string> label{};

    /// Minimum size (e.g., "512M", "1G", "10%")
    std::optional<std::string> size_min{};

    /// Maximum size (e.g., "2G", nullopt for unlimited)
    std::optional<std::string> size_max{};

    /// Filesystem format (e.g., "ext4", "btrfs", "xfs", "vfat", "swap")
    std::optional<std::string> format{};

    /// Encryption mode
    RepartEncryptMode encrypt{RepartEncryptMode::Off};

    /// Priority for space allocation (higher = lower priority)
    std::optional<std::int32_t> priority{};

    /// Weight for elastic space distribution
    std::optional<std::uint32_t> weight{};

    /// Btrfs subvolumes to create (paths within partition)
    std::vector<std::string> subvolumes{};

    /// Default btrfs subvolume
    std::optional<std::string> default_subvolume{};

    /// Whether the partition should grow to fill available space
    bool grow_filesystem{true};

    constexpr bool operator<=>(const RepartPartitionConfig&) const = default;
};

/// @brief Configuration flags for systemd-repart
struct RepartConfig final {
    /// Directory path for repart.d config files
    std::string definitions_dir{};

    /// Target device or image file
    std::string target_device{};

    /// Root directory for configuration lookup
    std::optional<std::string> root_dir{};

    /// Empty mode
    RepartEmptyMode empty_mode{RepartEmptyMode::Allow};

    /// Whether to perform a dry-run
    bool dry_run{true};

    /// Path to key file for LUKS encryption
    std::optional<std::string> key_file{};

    /// TPM2 device path (e.g., "auto" or "/dev/tpmrm0")
    std::optional<std::string> tpm2_device{};

    /// TPM2 PCR list for binding
    std::optional<std::string> tpm2_pcrs{};

    constexpr bool operator<=>(const RepartConfig&) const = default;
};

/// @brief Convert encryption mode to string for repart.d config
/// @param mode The encryption mode
/// @return The string representation
[[nodiscard]] auto encrypt_mode_to_string(RepartEncryptMode mode) noexcept -> std::string_view;

/// @brief Convert empty mode to string
/// @param mode The empty mode
/// @return The string representation
[[nodiscard]] auto empty_mode_to_string(RepartEmptyMode mode) noexcept -> std::string_view;

/// @brief Get repart partition type from filesystem type
/// @param fs_type The filesystem type
/// @param mountpoint The mountpoint
/// @return The repart type string
[[nodiscard]] auto get_repart_type(fs::FilesystemType fs_type, std::string_view mountpoint) noexcept -> std::string_view;

/// @brief Generate repart.d config file content for a partition
/// @param config The partition configuration
/// @return INI-format content string
[[nodiscard]] auto generate_repart_config_content(const RepartPartitionConfig& config) noexcept -> std::string;

/// @brief Write a single repart.d config file
/// @param dir_path The directory to write the config file to
/// @param filename The config filename
/// @param config The partition configuration
/// @return true on success, false overwise
[[nodiscard]] auto write_repart_config(std::string_view dir_path, std::string_view filename,
    const RepartPartitionConfig& config) noexcept -> bool;

/// @brief Write all repart.d config files for a partition scheme
/// @param dir_path The directory to write the config files to
/// @param partitions The partition configurations
/// @return true on success, false overwise
[[nodiscard]] auto write_repart_configs(std::string_view dir_path,
    const std::vector<RepartPartitionConfig>& partitions) noexcept -> bool;

/// @brief Execute systemd-repart with the given configuration
/// @param config The repart configuration
/// @return true on success, false overwise
[[nodiscard]] auto run_systemd_repart(const RepartConfig& config) noexcept -> bool;

/// @brief Preview systemd-repart changes
/// @param config The repart configuration
/// @return The pretty output of the changes
[[nodiscard]] auto preview_systemd_repart(const RepartConfig& config) noexcept -> std::string;

/// @brief Convert fs::Partition to RepartPartitionConfig
/// @param partition The source partition
/// @return The RepartPartitionConfig object
[[nodiscard]] auto convert_partition_to_repart(const fs::Partition& partition) noexcept -> RepartPartitionConfig;

/// @brief Convert partitions to repart partitions
/// @param partitions The source partitions
/// @return The vector of RepartPartitionConfig objects
[[nodiscard]] auto convert_partitions_to_repart(const std::vector<fs::Partition>& partitions) noexcept -> std::vector<RepartPartitionConfig>;

}  // namespace gucc::disk

#endif  // SYSTEMD_REPART_HPP
