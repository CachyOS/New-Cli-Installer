#ifndef KERNEL_PARAMS_HPP
#define KERNEL_PARAMS_HPP

#include "gucc/partition.hpp"

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::fs {

/// @brief Get kernel params from partitions information.
/// @param partitions The partitions information.
/// @param kernel_params The kernel params separated by space.
/// @param zfs_root_dataset The root dataset of ZFS.
/// @return A vector of strings representing the kernel parameters.
auto get_kernel_params(const std::vector<Partition>& partitions, std::string_view kernel_params, std::optional<std::string> zfs_root_dataset = std::nullopt) noexcept -> std::optional<std::vector<std::string>>;

}  // namespace gucc::fs

#endif  // KERNEL_PARAMS_HPP
