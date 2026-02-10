#ifndef INSTALLER_CONFIG_HPP
#define INSTALLER_CONFIG_HPP

#include <cstdint>      // for int32_t
#include <expected>     // for expected
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace installer {

/// Valid partition types.
enum class PartitionType : std::uint8_t {
    Root,
    Boot,
    Additional
};

/// Converts a string to PartitionType.
/// @param type_str The string representation of the partition type.
/// @return The PartitionType or std::nullopt if invalid.
[[nodiscard]] auto partition_type_from_string(std::string_view type_str) noexcept
    -> std::optional<PartitionType>;

/// Converts PartitionType to string.
[[nodiscard]] auto partition_type_to_string(PartitionType type) noexcept -> std::string_view;

/// Configuration for a single partition.
struct PartitionConfig {
    std::string name;
    std::string mountpoint;
    std::string size;
    std::string fs_name;
    PartitionType type{PartitionType::Additional};
};

/// Main installer configuration.
struct InstallerConfig {
    // Install type
    std::int32_t menus{2};
    bool headless_mode{false};
    bool server_mode{false};

    // Device and filesystem
    std::optional<std::string> device{};
    std::optional<std::string> fs_name{};
    std::vector<PartitionConfig> partitions{};
    std::optional<std::string> mount_opts{};

    // System settings
    std::optional<std::string> hostname{};
    std::optional<std::string> locale{};
    std::optional<std::string> xkbmap{};
    std::optional<std::string> timezone{};

    // User settings
    std::optional<std::string> user_name{};
    std::optional<std::string> user_pass{};
    std::optional<std::string> user_shell{};
    std::optional<std::string> root_pass{};

    // Packages
    std::optional<std::string> kernel{};
    std::optional<std::string> desktop{};
    std::optional<std::string> bootloader{};

    // Post-install
    std::optional<std::string> post_install{};
};

/// Parses installer configuration from JSON string content.
/// @param json_content The JSON configuration content.
/// @return InstallerConfig on success, or error string on failure.
[[nodiscard]] auto parse_installer_config(std::string_view json_content) noexcept
    -> std::expected<InstallerConfig, std::string>;

/// Validates that all required fields are present for headless mode.
/// @param config The configuration to validate.
/// @return void on success, or error string describing missing fields.
[[nodiscard]] auto validate_headless_config(const InstallerConfig& config) noexcept
    -> std::expected<void, std::string>;

/// Returns default InstallerConfig with sensible defaults.
[[nodiscard]] auto get_default_config() noexcept -> InstallerConfig;

}  // namespace installer

#endif  // INSTALLER_CONFIG_HPP
