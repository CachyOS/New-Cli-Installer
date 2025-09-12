#ifndef ZFS_TYPES_HPP
#define ZFS_TYPES_HPP

#include <optional>  // for optional
#include <string>    // for string
#include <vector>    // for vector

#include <fmt/format.h>
#include <fmt/ranges.h>

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

template <>
struct fmt::formatter<gucc::fs::ZfsDataset> : fmt::formatter<std::string> {
    // parse is inherited from fmt::formatter<std::string>.
    template <typename FormatContext>
    auto format(const gucc::fs::ZfsDataset& c, FormatContext& ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "(zpath:'{}', mountpoint:'{}')",
            c.zpath, c.mountpoint);
    }
};

template <>
struct fmt::formatter<gucc::fs::ZfsSetupConfig> : fmt::formatter<std::string> {
    // parse is inherited from fmt::formatter<std::string>.
    template <typename FormatContext>
    auto format(const gucc::fs::ZfsSetupConfig& c, FormatContext& ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "(zpool_name:'{}', zpool_options:'{}', datasets:{})",
            c.zpool_name, c.zpool_options, c.datasets);
    }
};

#endif  // ZFS_TYPES_HPP
