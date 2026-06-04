#ifndef ZFS_QUERY_HPP
#define ZFS_QUERY_HPP

#include <cstdint>      // for uint64_t
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::fs {

/// @brief Health status of a ZFS pool
enum class ZfsPoolHealth : std::uint8_t {
    Online,
    Degraded,
    Faulted,
    Offline,
    Unavailable,
    Suspended,
    Unknown
};

/// @brief Information about a ZFS pool
struct ZfsPoolInfo final {
    /// Pool name
    std::string name;
    /// Total pool size in bytes
    std::uint64_t size{0};
    /// Allocated space in bytes
    std::uint64_t allocated{0};
    /// Free space in bytes
    std::uint64_t free{0};
    /// Fragmentation percentage
    std::uint32_t fragmentation{0};
    /// Capacity used percentage
    std::uint32_t capacity{0};
    /// Pool health status
    ZfsPoolHealth health{ZfsPoolHealth::Unknown};
    /// Pool altroot if set
    std::optional<std::string> altroot;
    /// Devices in the pool
    std::vector<std::string> devices;
    /// Pool version
    std::optional<std::uint32_t> version;
    /// Whether the pool is bootable
    bool bootfs_set{false};
};

/// @brief Information about a ZFS dataset
struct ZfsDatasetInfo final {
    /// Full dataset path (e.g., "cos/home")
    std::string name;
    /// Mount point (or "legacy" or "none")
    std::string mountpoint;
    /// Space used in bytes
    std::uint64_t used{0};
    /// Space available in bytes
    std::uint64_t available{0};
    /// Referenced space in bytes
    std::uint64_t referenced{0};
    /// Compression algorithm
    std::optional<std::string> compression;
    /// Whether encryption is enabled
    bool encryption{false};
    /// Record size
    std::optional<std::uint32_t> recordsize;
    /// Dataset type (filesystem, volume, snapshot)
    std::string type;
    /// Whether the dataset is mounted
    bool mounted{false};
};

/// @brief Convert ZfsPoolHealth enum to string representation
/// @param health The pool health status to convert
/// @return String representation of the health status
auto zfs_pool_health_to_string(ZfsPoolHealth health) noexcept -> std::string_view;

/// @brief Convert string to ZfsPoolHealth enum
/// @param health_str The health string (e.g., "ONLINE", "DEGRADED")
/// @return ZfsPoolHealth enum value
auto string_to_zfs_pool_health(std::string_view health_str) noexcept -> ZfsPoolHealth;

/// @brief Lists all imported ZFS pools with detailed information
/// @return Vector of ZfsPoolInfo objects
auto list_zfs_pools() noexcept -> std::vector<ZfsPoolInfo>;

/// @brief Gets detailed information for a specific ZFS pool
/// @param pool_name The name of the pool
/// @return Optional ZfsPoolInfo, std::nullopt if pool not found
auto get_zfs_pool_info(std::string_view pool_name) noexcept -> std::optional<ZfsPoolInfo>;

/// @brief Lists all datasets in a ZFS pool
/// @param pool_name Optional pool name to filter by, lists all if empty
/// @return Vector of ZfsDatasetInfo objects
auto list_zfs_datasets(std::string_view pool_name = "") noexcept -> std::vector<ZfsDatasetInfo>;

/// @brief Gets detailed information for a specific ZFS dataset
/// @param dataset_path The full dataset path (e.g., "tank/home")
/// @return Optional ZfsDatasetInfo, std::nullopt if dataset not found
auto get_zfs_dataset_info(std::string_view dataset_path) noexcept -> std::optional<ZfsDatasetInfo>;

/// @brief Checks if ZFS is available on the system
/// @return True if zfs and zpool commands are available
auto is_zfs_available() noexcept -> bool;

}  // namespace gucc::fs

namespace gucc::fs::zfs_query::detail {

/// @brief Parses a size string from `zpool list -Hp` / `zfs list -Hp` output.
auto parse_zfs_size(std::string_view size_str) noexcept -> std::uint64_t;

/// @brief Split a tab-separated line preserving empty cells,
auto split_tsv(std::string_view line) noexcept -> std::vector<std::string>;

/// @brief Decode a fully-tabular `zfs list -Hp` line into a
/// ZfsDatasetInfo.
auto parse_dataset_fields(std::vector<std::string> fields) noexcept
    -> std::optional<ZfsDatasetInfo>;

}  // namespace gucc::fs::zfs_query::detail

#endif  // ZFS_QUERY_HPP
