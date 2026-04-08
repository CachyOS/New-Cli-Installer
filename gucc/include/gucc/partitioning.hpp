#ifndef PARTITIONING_HPP
#define PARTITIONING_HPP

#include "gucc/export.hpp"

#include "gucc/error.hpp"
#include "gucc/partition.hpp"
#include "gucc/partition_config.hpp"

#include <cstdint>      // for uint32_t, uint64_t
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

GUCC_EXPORT_BEGIN

namespace gucc::disk {

// Erases disk
auto erase_disk(std::string_view device) noexcept -> Result<void>;

/// Securely overwrite every block of device.
auto secure_wipe(std::string_view device) noexcept -> Result<void>;

// Generates sfdisk commands from Partition scheme
auto gen_sfdisk_command(const std::vector<fs::Partition>& partitions, bool is_efi) noexcept -> std::string;

// Runs disk partitioning using sfdisk command on device
auto run_sfdisk_part(std::string_view commands, std::string_view device) noexcept -> Result<void>;

// Runs disk partitioning using Partition scheme and erasing device
auto make_clean_partschema(std::string_view device, const std::vector<fs::Partition>& partitions, bool is_efi) noexcept -> Result<void>;

/// @brief Generates a partition schema from configuration
/// @param device The target device (e.g., "/dev/sda")
/// @param config The partition schema configuration
/// @param is_efi Whether the system is UEFI or BIOS
/// @return Vector of Partition objects representing the schema
auto generate_partition_schema_from_config(std::string_view device, const fs::DefaultPartitionSchemaConfig& config, bool is_efi) noexcept -> std::vector<fs::Partition>;

/// @brief Validates a partition schema without applying it
/// @param partitions The partition schema to validate
/// @param device The target device
/// @param is_efi Whether the system is UEFI
/// @return report with any errors or warnings
auto validate_partition_schema(const std::vector<fs::Partition>& partitions, std::string_view device, bool is_efi) noexcept -> fs::PartitionSchemaValidation;

/// @brief Generates a human-readable preview of the partition schema
/// @param partitions The partition schema to preview
/// @param device The target device
/// @param is_efi Whether the system is UEFI
/// @return Formatted string showing what will be created
auto preview_partition_schema(const std::vector<fs::Partition>& partitions, std::string_view device, bool is_efi) noexcept -> std::string;

/// Writes an empty @p table_type ("gpt"/"msdos") table.
/// @warning drops every partition on @p device.
auto create_partition_table(std::string_view device, std::string_view table_type) noexcept -> Result<void>;

/// Creates a partition inside the free byte range [@p start_bytes, @p end_bytes].
/// @return device node of the new partition.
auto create_partition(std::string_view device, std::uint64_t start_bytes, std::uint64_t end_bytes) noexcept -> Result<std::string>;

auto delete_partition(std::string_view device, std::uint32_t number) noexcept -> Result<void>;

auto set_partition_flag(std::string_view device, std::uint32_t number, std::string_view flag, bool state) noexcept -> Result<void>;

/// Shrinks the filesystem to @p new_size_bytes, then moves the partition end to @p new_end_bytes.
/// Only ext4 and ntfs can be shrunk offline.
auto shrink_partition(std::string_view device, std::uint32_t number, std::string_view fstype,
    std::uint64_t new_size_bytes, std::uint64_t new_end_bytes) noexcept -> Result<void>;

/// Bytes the unmounted filesystem on @p partition occupies, ext4 and ntfs only.
auto filesystem_used_bytes(std::string_view partition, std::string_view fstype) noexcept -> Result<std::uint64_t>;

auto set_filesystem_label(std::string_view partition, std::string_view fstype, std::string_view label) noexcept -> Result<void>;

}  // namespace gucc::disk

GUCC_EXPORT_END

#endif  // PARTITIONING_HPP
