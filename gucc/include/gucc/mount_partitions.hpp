#ifndef MOUNT_PARTITIONS_HPP
#define MOUNT_PARTITIONS_HPP

#include <cinttypes>  // for int32_t

#include <string>       // for string
#include <string_view>  // for string_view

namespace gucc::mount {

// Mount partition
auto mount_partition(std::string_view partition, std::string_view mount_dir, std::string_view mount_opts) noexcept -> bool;
// Query partition
auto query_partition(std::string_view partition, std::int32_t& is_luks, std::int32_t& is_lvm, std::string& luks_name, std::string& luks_dev, std::string& luks_uuid) noexcept -> bool;

}  // namespace gucc::mount

#endif  // MOUNT_PARTITIONS_HPP
