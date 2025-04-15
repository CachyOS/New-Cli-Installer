#ifndef ZFS_TYPES_HPP
#define ZFS_TYPES_HPP

#include <optional>  // for optional
#include <string>    // for string
#include <vector>    // for vector

namespace gucc::fs {

/// @brief Represents a single ZFS dataset, defining its ZFS path and mount point.
struct ZfsDataset {
    /// @brief The ZFS path for the dataset (e.g., "poolname/datasetname", "poolname/parent/child").
    ///
    /// @note This is the identifier used within the ZFS hierarchy.
    std::string zpath;

    /// @brief The desired filesystem mount point for this dataset (e.g., "/", "/home").
    std::string mountpoint;
};

/// @brief Configuration structure for setting up a ZFS pool and its initial datasets.
///
/// @note This struct aggregates all the necessary parameters required by the
/// zfs_create_with_config function to create a ZFS pool and optionally
/// populate it with predefined datasets.
struct ZfsSetupConfig {
    /// @brief The desired name for the ZFS pool (e.g., "zpool", "data").
    std::string zpool_name;

    /// @brief A string containing ZFS pool creation options.
    ///
    /// e.g. "-o ashift=12 -O compression=zstd"
    std::string zpool_options;

    /// @brief An optional passphrase for ZFS native encryption.
    std::optional<std::string> passphrase;

    /// @brief A list of ZFS datasets to create within the pool immediately after
    /// the pool itself is created.
    std::vector<ZfsDataset> datasets;
};

}  // namespace gucc::fs

#endif  // ZFS_TYPES_HPP
