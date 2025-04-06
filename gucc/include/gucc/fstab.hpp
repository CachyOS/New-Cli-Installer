#ifndef FSTAB_HPP
#define FSTAB_HPP

#include "gucc/partition.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::fs {

// Generate fstab
auto generate_fstab(const std::vector<Partition>& partitions, std::string_view root_mountpoint) noexcept -> bool;

// Generate fstab into string
auto generate_fstab_content(const std::vector<Partition>& partitions) noexcept -> std::string;

// Generates fstab using genfstab -U -p
// NOTE: this func should only be used in cases where we don't know about whole partition schema
auto run_genfstab_on_mount(std::string_view root_mountpoint) noexcept -> bool;

}  // namespace gucc::fs

#endif  // FSTAB_HPP
