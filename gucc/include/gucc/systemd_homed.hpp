#ifndef SYSTEMD_HOMED_HPP
#define SYSTEMD_HOMED_HPP

#include <cstdint>      // for uint8_t, uint32_t
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::homed {

/// Storage backend for home directories
enum class StorageType : std::uint8_t {
    /// LUKS2 encrypted loopback file
    Luks,
    /// Native filesystem encryption
    Fscrypt,
    /// Plain directory, no encryption
    Directory,
    /// Btrfs subvolume, no encryption
    Subvolume,
    /// Network CIFS share
    Cifs
};

/// Filesystem type for LUKS storage backend
enum class LuksFilesystemType : std::uint8_t {
    Ext4,
    Btrfs,
    Xfs
};

/// User configuration for homectl
struct HomedUserConfig final {
    /// Username
    std::string_view username;

    /// User password
    std::string_view password;

    /// Real name
    std::string_view real_name{};

    /// Login shell
    std::string_view shell{};

    /// Preferred UID
    std::optional<std::uint32_t> uid{};

    /// Additional groups to join
    std::vector<std::string> groups{};

    /// Storage backend type
    StorageType storage{StorageType::Luks};

    /// Custom path to home directory image (LUKS) or directory
    std::optional<std::string> image_path{};

    /// Custom home directory mountpoint
    std::optional<std::string> home_dir{};

    /// Filesystem inside LUKS container
    LuksFilesystemType luks_fs_type{LuksFilesystemType::Ext4};

    /// Disk size limit
    std::optional<std::string> disk_size{};

    /// Enable discard/TRIM while mounted
    bool luks_discard{false};

    /// Enable discard on deactivation
    bool luks_offline_discard{true};

    constexpr bool operator<=>(const HomedUserConfig&) const = default;
};

/// @brief Convert StorageType enum to homectl argument string
/// @param type The storage type
/// @return String representation
[[nodiscard]] auto storage_type_to_string(StorageType type) noexcept -> std::string_view;

/// @brief Convert LuksFilesystemType enum to string
/// @param type The filesystem type
/// @return String representation
[[nodiscard]] auto luks_fs_type_to_string(LuksFilesystemType type) noexcept -> std::string_view;

/// @brief Generate homectl command from configuration
/// @param config The user configuration
/// @return The command-line arguments for homectl
[[nodiscard]] auto generate_homectl_args(const HomedUserConfig& config) noexcept -> std::vector<std::string>;

/// @brief Create a new user with homectl
/// @param config The user configuration
/// @return true on success, false otherwise
[[nodiscard]] auto create_homed_user(const HomedUserConfig& config) noexcept -> bool;

/// @brief Activate (unlock and mount) a user's home directory
/// @param username The username to activate
/// @return true on success, false otherwise
[[nodiscard]] auto activate_home(std::string_view username) noexcept -> bool;

/// @brief Deactivate (unmount and lock) a user's home directory
/// @param username The username to deactivate
/// @return true on success, false otherwise
[[nodiscard]] auto deactivate_home(std::string_view username) noexcept -> bool;

/// @brief Remove a homed-managed user and their home directory
/// @param username The username to remove
/// @return true on success, false otherwise
[[nodiscard]] auto remove_homed_user(std::string_view username) noexcept -> bool;

}  // namespace gucc::homed

#endif  // SYSTEMD_HOMED_HPP
