#ifndef CRYPTTAB_HPP
#define CRYPTTAB_HPP

#include "gucc/partition.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::fs {

// Generate crypttab
auto generate_crypttab(const std::vector<Partition>& partitions, std::string_view root_mountpoint, std::string_view crypttab_opts) noexcept -> bool;

// Generate crypttab into string
auto generate_crypttab_content(const std::vector<Partition>& partitions, std::string_view crypttab_opts) noexcept -> std::string;

}  // namespace gucc::fs

#endif  // CRYPTTAB_HPP
