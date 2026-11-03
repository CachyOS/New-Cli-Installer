#ifndef SWAP_HPP
#define SWAP_HPP

#include "gucc/export.hpp"

#include "gucc/error.hpp"
#include "gucc/partition.hpp"

#include <string_view>  // for string_view

GUCC_EXPORT_BEGIN

namespace gucc::swap {

// Creates swapfile on mountpoint
auto make_swapfile(std::string_view root_mountpoint, std::string_view swap_size) noexcept -> Result<void>;

/// Deactivates swap on device
auto swapoff(std::string_view device) noexcept -> Result<void>;

// Creates swap file from existing parition
auto make_swap_partition(const fs::Partition& swap_partition) noexcept -> Result<void>;

}  // namespace gucc::swap

GUCC_EXPORT_END

#endif  // SWAP_HPP
