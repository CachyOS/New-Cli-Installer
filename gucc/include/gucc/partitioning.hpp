#ifndef PARTITIONING_HPP
#define PARTITIONING_HPP

#include "gucc/partition.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::disk {

// Generates sfdisk commands from Partition scheme
auto gen_sfdisk_command(const std::vector<fs::Partition>& partitions, bool is_efi) noexcept -> std::string;

// Runs disk partitioning using sfdisk command on device
auto run_sfdisk_part(std::string_view commands, std::string_view device) noexcept -> bool;
}  // namespace gucc::disk

#endif  // PARTITIONING_HPP
