#ifndef PARTITIONING_HPP
#define PARTITIONING_HPP

#include "gucc/error.hpp"
#include "gucc/partition.hpp"
#include "gucc/partition_config.hpp"

#include <cstdint>      // for uint64_t
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::disk {

/// @brief An unallocated (free) region on a disk, in bytes.
struct FreeRegion {
    std::uint64_t start_bytes{};  ///< first byte of the gap
    std::uint64_t end_bytes{};    ///< last byte of the gap (inclusive)
    std::uint64_t size_bytes{};   ///< gap size in bytes
};

/// @brief Parses the free regions out of `parted -m ... unit B print free` output.
///
/// Pure (no I/O) so it can be unit-tested with captured `parted` output. Only
/// lines whose type field is `free` are returned; the disk/partition lines are
/// ignored.
[[nodiscard]] auto parse_free_regions(std::string_view parted_machine_output) noexcept
    -> std::vector<FreeRegion>;

/// @brief Lists the unallocated free regions on a device (non-destructive).
///
/// Runs `parted -m -s <device> unit B print free` and parses the result. Returns
/// an empty vector if the device has no free space or parted fails.
[[nodiscard]] auto list_free_regions(std::string_view device) noexcept
    -> std::vector<FreeRegion>;

/// @brief Creates one new partition filling the byte range [start, end] on a
/// device, without touching existing partitions, and returns its device path.
///
/// Non-destructive to existing data: it only writes a new entry into the
/// unallocated gap. Intended for the "install alongside" flow where the user
/// installs into existing free space rather than shrinking anything.
/// @return the new partition path (e.g. /dev/sda3), or an error.
[[nodiscard]] auto create_partition_in_region(std::string_view device,
    std::uint64_t start_bytes, std::uint64_t end_bytes) noexcept
    -> Result<std::string>;

/// @brief An existing partition's on-disk layout, parsed from `parted -m print`.
///
/// Distinct from gucc::disk::PartitionInfo (system_query.hpp), which is the
/// lsblk-derived view; this one carries the byte range + flags the editor needs.
struct PartitionLayout {
    std::uint32_t number{};       ///< 1-based partition number
    std::uint64_t start_bytes{};  ///< first byte
    std::uint64_t end_bytes{};    ///< last byte (inclusive)
    std::uint64_t size_bytes{};   ///< size in bytes
    std::string fstype{};         ///< filesystem parted reports ("ext4", "ntfs", "" if none)
    std::string name{};           ///< GPT partition name (parted's name field)
    std::string flags{};          ///< comma-separated flags ("boot, esp")
};

/// @brief Parses the partition records from `parted -m -s <dev> unit B print` output.
///
/// Pure (no I/O), unit-testable. Ignores the `BYT;`/disk header and any free-space
/// records (see parse_free_regions for those).
[[nodiscard]] auto parse_device_partitions(std::string_view parted_machine_output) noexcept
    -> std::vector<PartitionLayout>;

/// @brief Lists existing partitions on a device (`parted -m print` + parse).
[[nodiscard]] auto list_device_partitions(std::string_view device) noexcept
    -> std::vector<PartitionLayout>;

/// @brief Filesystem resize command (`resize2fs`/`ntfsresize`/`btrfs filesystem
/// resize`) to set @p partition to @p new_size_bytes. Empty if @p fstype can't be
/// resized (xfs shrink, swap, vfat, …).
[[nodiscard]] auto build_resize_filesystem_command(std::string_view partition,
    std::string_view fstype, std::uint64_t new_size_bytes) noexcept -> std::string;

/// @brief `parted resizepart` command moving partition @p number's end boundary.
[[nodiscard]] auto build_resize_partition_command(std::string_view device,
    std::uint32_t number, std::uint64_t new_end_bytes) noexcept -> std::string;

/// @brief `parted mkpart` command for a new partition spanning [start, end].
[[nodiscard]] auto build_create_partition_command(std::string_view device,
    std::uint64_t start_bytes, std::uint64_t end_bytes, std::string_view parted_fs) noexcept -> std::string;

/// @brief `parted rm <number>` command.
[[nodiscard]] auto build_delete_partition_command(std::string_view device,
    std::uint32_t number) noexcept -> std::string;

/// @brief `parted set <number> <flag> on|off` command.
[[nodiscard]] auto build_set_flag_command(std::string_view device, std::uint32_t number,
    std::string_view flag, bool state) noexcept -> std::string;

/// @brief `parted mklabel <gpt|msdos>` command (a fresh, empty partition table).
[[nodiscard]] auto build_create_table_command(std::string_view device,
    std::string_view table_type) noexcept -> std::string;

/// @brief Resizes the filesystem on @p partition to @p new_size_bytes.
[[nodiscard]] auto resize_filesystem(std::string_view partition, std::string_view fstype,
    std::uint64_t new_size_bytes) noexcept -> Result<void>;

/// @brief Moves partition @p number's end boundary to @p new_end_bytes.
[[nodiscard]] auto resize_partition(std::string_view device, std::uint32_t number,
    std::uint64_t new_end_bytes) noexcept -> Result<void>;

/// @brief Shrinks a partition safely: resizes the filesystem first, then the
/// partition table entry (the order that avoids truncating live data).
[[nodiscard]] auto shrink_partition(std::string_view device, std::uint32_t number,
    std::string_view partition, std::string_view fstype,
    std::uint64_t new_size_bytes, std::uint64_t new_end_bytes) noexcept -> Result<void>;

/// @brief Bytes currently in use by the filesystem on @p partition — the floor a
/// shrink slider must not cross. Uses `dumpe2fs`/`ntfsresize -i`/`btrfs`.
[[nodiscard]] auto filesystem_used_bytes(std::string_view partition,
    std::string_view fstype) noexcept -> Result<std::uint64_t>;

/// @brief Deletes partition @p number (parted rm).
[[nodiscard]] auto delete_partition(std::string_view device, std::uint32_t number) noexcept -> Result<void>;

/// @brief Formats @p partition as @p fstype with an optional label (mkfs).
[[nodiscard]] auto format_partition(std::string_view partition, std::string_view fstype,
    std::string_view label) noexcept -> Result<void>;

/// @brief Sets/clears a partition flag (boot, esp, …) on partition @p number.
[[nodiscard]] auto set_partition_flag(std::string_view device, std::uint32_t number,
    std::string_view flag, bool state) noexcept -> Result<void>;

/// @brief Sets the filesystem label on @p partition (fs-specific tool).
[[nodiscard]] auto set_filesystem_label(std::string_view partition, std::string_view fstype,
    std::string_view label) noexcept -> Result<void>;

/// @brief Writes a fresh, empty partition table of @p table_type ("gpt"/"msdos").
[[nodiscard]] auto create_partition_table(std::string_view device,
    std::string_view table_type) noexcept -> Result<void>;

// Erases disk
auto erase_disk(std::string_view device) noexcept -> Result<void>;

// Generates sfdisk commands from Partition scheme
auto gen_sfdisk_command(const std::vector<fs::Partition>& partitions, bool is_efi) noexcept -> std::string;

// Runs disk partitioning using sfdisk command on device
auto run_sfdisk_part(std::string_view commands, std::string_view device) noexcept -> Result<void>;

// Generates a default partition schema for the device
// For BIOS: Creates a single root partition
// For UEFI: Creates a root partition and an EFI system partition (efi_partition_size,
// default 2GiB) at the specified mount point
auto generate_default_partition_schema(std::string_view device, std::string_view boot_mountpoint, bool is_efi, std::string_view efi_partition_size = "2GiB") noexcept -> std::vector<fs::Partition>;

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

}  // namespace gucc::disk

#endif  // PARTITIONING_HPP
