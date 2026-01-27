#ifndef PARTITIONING_HPP
#define PARTITIONING_HPP

#include "gucc/partition.hpp"
#include "gucc/partition_config.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::disk {

// Erases disk
auto erase_disk(std::string_view device) noexcept -> bool;

// Generates sfdisk commands from Partition scheme
auto gen_sfdisk_command(const std::vector<fs::Partition>& partitions, bool is_efi) noexcept -> std::string;

// Runs disk partitioning using sfdisk command on device
auto run_sfdisk_part(std::string_view commands, std::string_view device) noexcept -> bool;

// Generates a default partition schema for the device
// For BIOS: Creates a single root partition
// For UEFI: Creates a root partition and a 2GiB EFI system partition at the specified mount point
auto generate_default_partition_schema(std::string_view device, std::string_view boot_mountpoint, bool is_efi) noexcept -> std::vector<fs::Partition>;

// Runs disk partitioning using Partition scheme and erasing device
auto make_clean_partschema(std::string_view device, const std::vector<fs::Partition>& partitions, bool is_efi) noexcept -> bool;

/// @brief Generates a partition schema from configuration
/// @param device The target device (e.g., "/dev/sda")
/// @param config The partition schema configuration
/// @param is_efi Whether the system is UEFI or BIOS
/// @return Vector of Partition objects representing the schema
auto generate_partition_schema_from_config(std::string_view device, const fs::DefaultPartitionSchemaConfig& config, bool is_efi) noexcept -> std::vector<fs::Partition>;

}  // namespace gucc::disk

#endif  // PARTITIONING_HPP
