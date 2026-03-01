#ifndef MOUNT_PARTITIONS_HPP
#define MOUNT_PARTITIONS_HPP

#include "gucc/partition.hpp"

#include <cinttypes>  // for int32_t
#include <optional>   // for optional

#include <string>       // for string
#include <string_view>  // for string_view

namespace gucc::mount {

// Mount partition
auto mount_partition(std::string_view partition, std::string_view mount_dir, std::string_view mount_opts) noexcept -> bool;
// Query partition
auto query_partition(std::string_view partition, std::int32_t& is_luks, std::int32_t& is_lvm, std::string& luks_name, std::string& luks_dev, std::string& luks_uuid) noexcept -> bool;

/// @brief Formats, mounts, and creates a Partition entry for an ESP
/// @param device The partition device path
/// @param mountpoint The ESP mountpoint relative to root
/// @param base_mountpoint The base mountpoint of the installation
/// @param format Whether to format the partition
/// @param is_ssd Whether the device is an SSD
/// @return Partition struct on success, nullopt on failure
auto setup_esp_partition(std::string_view device, std::string_view mountpoint, std::string_view base_mountpoint, bool format, bool is_ssd) noexcept -> std::optional<fs::Partition>;

}  // namespace gucc::mount

#endif  // MOUNT_PARTITIONS_HPP
