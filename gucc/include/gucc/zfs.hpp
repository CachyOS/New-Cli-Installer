#ifndef ZFS_HPP
#define ZFS_HPP

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::fs {

struct ZfsDataset final {
    std::string zpath;
    std::string mountpoint;
};

// Creates a zfs volume
auto zfs_create_zvol(std::string_view zsize, std::string_view zpath) noexcept -> bool;

// Creates a zfs filesystem, the first parameter is the ZFS path and the second is the mount path
auto zfs_create_dataset(std::string_view zpath, std::string_view zmount) noexcept -> bool;

// Creates a zfs datasets from predefined scheme
auto zfs_create_datasets(const std::vector<ZfsDataset>& zdatasets) noexcept -> bool;

auto zfs_destroy_dataset(std::string_view zdataset) noexcept -> bool;

// returns a list of imported zpools
auto zfs_list_pools() noexcept -> std::string;

// returns a list of devices containing zfs members
auto zfs_list_devs() noexcept -> std::string;

auto zfs_list_datasets(std::string_view type = "none") noexcept -> std::string;

// Sets zfs property
auto zfs_set_property(std::string_view property, std::string_view dataset) noexcept -> bool;

// Sets zfs pool property
auto zpool_set_property(std::string_view property, std::string_view pool_name) noexcept -> bool;

/// @brief Creates a zpool with the specified arguments.
/// @param device_path The full path to the device to create on.
/// @param pool_name The name of the pool.
/// @param pool_options The options of the pool.
/// @param passphrase The optional password for the pool, in case the password specified zpool is created with encryption.
/// @return True if the creation was successful, false otherwise.
auto zfs_create_zpool(std::string_view device_path, std::string_view pool_name, std::string_view pool_options, std::optional<std::string_view> passphrase = std::nullopt) noexcept -> bool;

}  // namespace gucc::fs

#endif  // ZFS_HPP
