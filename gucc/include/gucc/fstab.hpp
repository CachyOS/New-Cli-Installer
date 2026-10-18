#pragma once

#include "gucc/error.hpp"
#include "gucc/partition.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::fs {

// Generate fstab
auto generate_fstab(const std::vector<Partition>& partitions, std::string_view root_mountpoint) noexcept -> Result<void>;

// Generate fstab into string
auto generate_fstab_content(const std::vector<Partition>& partitions) noexcept -> std::string;

// Generates fstab using genfstab -U -p
// NOTE: this func should only be used in cases where we don't know about whole partition schema
auto run_genfstab_on_mount(std::string_view root_mountpoint) noexcept -> Result<void>;

}  // namespace gucc::fs

namespace gucc::fs::detail {

auto comment_out_zfs_entries(std::string_view fstab_content, const std::vector<std::string>& zfs_datasets) noexcept -> std::string;
auto strip_root_prefix(std::string_view fstab_content, std::string_view root_mountpoint) noexcept -> std::string;
auto strip_subvolid_options(std::string_view fstab_content) noexcept -> std::string;

}  // namespace gucc::fs::detail
