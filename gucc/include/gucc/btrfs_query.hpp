#ifndef BTRFS_QUERY_HPP
#define BTRFS_QUERY_HPP

#include "gucc/btrfs.hpp"

#include <cstdint>      // for uint64_t
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::fs {

/// @brief Detailed information for one btrfs subvolume (or snapshot).
struct BtrfsSubvolumeInfo final {
    /// Subvolume ID
    std::uint64_t id{0};
    /// Generation number
    std::uint64_t gen{0};
    /// Parent subvolume ID (for snapshots)
    std::uint64_t parent_id{0};
    /// Top level ID
    std::uint64_t top_level{0};
    /// Subvolume path
    std::string path;
    /// UUID of the subvolume
    std::optional<std::string> uuid;
    /// Parent UUID (for snapshots, refers to source subvolume)
    std::optional<std::string> parent_uuid;
    /// Whether this is a read-only snapshot
    bool is_readonly{false};
    /// Creation time (if available)
    std::optional<std::string> creation_time;
};

/// @brief Information about btrfs filesystem usage
struct BtrfsFilesystemInfo final {
    /// Device path
    std::string device;
    /// Total size of the filesystem in bytes
    std::uint64_t total_size{0};
    /// Used space in bytes
    std::uint64_t used{0};
    /// Free space in bytes
    std::uint64_t free{0};
    /// Allocation profile
    std::string data_profile;
    std::string metadata_profile;
    /// UUID of the btrfs filesystem
    std::optional<std::string> uuid;
    /// Label of the btrfs filesystem
    std::optional<std::string> label;
};

/// @brief Lists all btrfs subvolumes on a mounted btrfs filesystem
/// @param mountpoint Path to the mounted btrfs filesystem
/// @param filter_prefix Optional prefix filter, only include subvolumes
///                      whose path starts with this prefix. Default "@"
///                      matches the typical root layout
///                      (@, @home, @log, ...) and excludes anonymous
///                      docker/container subvolumes. Pass empty string
///                      to include every subvolume.
/// @return Vector of BtrfsSubvolume objects
auto list_btrfs_subvolumes(std::string_view mountpoint, std::string_view filter_prefix = "@") noexcept -> std::vector<BtrfsSubvolume>;

/// @brief Lists btrfs snapshots (subvolumes with parent_uuid set)
/// @param mountpoint Path to the mounted btrfs filesystem
/// @param filter_prefix Optional prefix filter for snapshot paths (default: "@")
/// @return Vector of BtrfsSubvolumeInfo for entries that are snapshots
auto list_btrfs_snapshots(std::string_view mountpoint, std::string_view filter_prefix = "@") noexcept -> std::vector<BtrfsSubvolumeInfo>;

/// @brief Gets all subvolume information including IDs and UUIDs
/// @param mountpoint Path to the mounted btrfs filesystem
/// @param filter_prefix Optional prefix filter for subvolume paths (default: "@")
/// @return Vector of BtrfsSubvolumeInfo (every matching subvolume, snapshot or not)
auto list_btrfs_subvolumes_detailed(std::string_view mountpoint, std::string_view filter_prefix = "@") noexcept -> std::vector<BtrfsSubvolumeInfo>;

/// @brief Gets btrfs filesystem usage information
/// @param mountpoint_or_device Path to mounted btrfs or device path
/// @return Optional BtrfsFilesystemInfo, std::nullopt on failure
auto get_btrfs_info(std::string_view mountpoint_or_device) noexcept -> std::optional<BtrfsFilesystemInfo>;

/// @brief Checks if a path is a btrfs filesystem
/// @param path Path to check
/// @return True if the path is on a btrfs filesystem
auto is_btrfs(std::string_view path) noexcept -> bool;

}  // namespace gucc::fs

namespace gucc::fs::btrfs_query::detail {

/// @brief Parses output of `btrfs subvolume list`.
auto parse_subvolume_list(std::string_view output, std::string_view filter_prefix) noexcept
    -> std::vector<BtrfsSubvolumeInfo>;

}  // namespace gucc::fs::btrfs_query::detail

#endif  // BTRFS_QUERY_HPP
