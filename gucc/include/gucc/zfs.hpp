#ifndef ZFS_HPP
#define ZFS_HPP

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

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
    std::vector<gucc::fs::ZfsDataset> datasets;
};

/// @brief Creates a ZFS volume (zvol).
/// @param zsize The size of the volume (e.g., "10G").
/// @param zpath The ZFS path for the volume (e.g., "pool/volume").
/// @return True on success, false otherwise.
auto zfs_create_zvol(std::string_view zsize, std::string_view zpath) noexcept -> bool;

/// @brief Creates a ZFS filesystem dataset.
/// @param zpath The ZFS path for the dataset (e.g., "pool/home").
/// @param zmount The mount point for the dataset (e.g., "/mnt/home").
/// @return True on success, false otherwise.
auto zfs_create_dataset(std::string_view zpath, std::string_view zmount) noexcept -> bool;

/// @brief Creates multiple ZFS datasets based on a list.
/// @param zdatasets A vector of ZfsDataset definitions.
/// @return True if all datasets were created successfully, false otherwise.
auto zfs_create_datasets(const std::vector<ZfsDataset>& zdatasets) noexcept -> bool;

/// @brief Destroys a ZFS dataset or volume.
/// @param zdataset The ZFS path of the dataset/volume to destroy.
/// @return True on success, false otherwise.
auto zfs_destroy_dataset(std::string_view zdataset) noexcept -> bool;

/// @brief Lists imported ZFS pools.
/// @return A string containing the names of imported pools, typically one per line.
auto zfs_list_pools() noexcept -> std::string;

/// @brief Lists block devices that are part of ZFS pools.
/// @return A string containing the device paths.
auto zfs_list_devs() noexcept -> std::string;

/// @brief Lists ZFS datasets.
/// @param type Optional filter by type (e.g., "zvol", "legacy"). "none" or empty lists all.
/// @return A string containing the dataset paths.
auto zfs_list_datasets(std::string_view type = "none") noexcept -> std::string;

/// @brief Sets a property on a ZFS dataset or volume.
/// @param property The property setting (e.g., "compression=lz4").
/// @param dataset The ZFS path of the target dataset/volume.
/// @return True on success, false otherwise.
auto zfs_set_property(std::string_view property, std::string_view dataset) noexcept -> bool;

/// @brief Sets a property on a ZFS pool.
/// @param property The property setting (e.g., "ashift=12").
/// @param pool_name The name of the target pool.
/// @return True on success, false otherwise.
auto zpool_set_property(std::string_view property, std::string_view pool_name) noexcept -> bool;

/// @brief Creates a zpool with the specified arguments.
/// @param device_path The full path to the device to create on.
/// @param pool_name The name of the pool.
/// @param pool_options The options of the pool.
/// @param passphrase The optional password for the pool, in case the password specified zpool is created with encryption.
/// @return True if the creation was successful, false otherwise.
auto zfs_create_zpool(std::string_view device_path, std::string_view pool_name, std::string_view pool_options, std::optional<std::string_view> passphrase = std::nullopt) noexcept -> bool;

/// @brief Creates a ZFS pool and specified datasets from a configuration struct.
/// @param device_path The full path to the device to create on.
/// @param zfs_config Configuration containing pool details and datasets to create.
/// @return True if the pool and all datasets are created successfully, false otherwise.
auto zfs_create_with_config(std::string_view device_path, const fs::ZfsSetupConfig& zfs_config) noexcept -> bool;

}  // namespace gucc::fs

#endif  // ZFS_HPP
