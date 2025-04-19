#ifndef SWAP_HPP
#define SWAP_HPP

#include "gucc/partition.hpp"

#include <string_view>  // for string_view

namespace gucc::swap {

// Creates swapfile on mountpoint
auto make_swapfile(std::string_view root_mountpoint, std::string_view swap_size) noexcept -> bool;

// Creates swap file from existing parition
auto make_swap_partition(const fs::Partition& swap_partition) noexcept -> bool;

}  // namespace gucc::swap

#endif  // SWAP_HPP
