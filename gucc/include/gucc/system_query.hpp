#ifndef SYSTEM_QUERY_HPP
#define SYSTEM_QUERY_HPP

#include "gucc/partition.hpp"

#include <cstdint>      // for uint64_t
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::disk {

/// @brief Transport type for storage devices
enum class DiskTransport : std::uint8_t {
    Sata,
    Nvme,
    Usb,
    Scsi,
    Virtio,
    Unknown
};

/// @brief Information about a partition on a disk
struct PartitionInfo final {
    /// Partition device path
    std::string device;
    /// Filesystem type
    std::string fstype;
    /// Partition label
    std::optional<std::string> label;
    /// Filesystem UUID
    std::optional<std::string> uuid;
    /// Partition UUID
    std::optional<std::string> partuuid;
    /// Partition size in bytes
    std::uint64_t size{0};
    /// Mountpoint if mounted
    std::optional<std::string> mountpoint;
    /// Whether the partition is currently mounted
    bool is_mounted{false};
    /// Partition number on the disk
    std::uint32_t part_number{0};
};

/// @brief Information about a disk device
struct DiskInfo final {
    /// Disk device path
    std::string device;
    /// Disk model name
    std::optional<std::string> model;
    /// Total disk size in bytes
    std::uint64_t size{0};
    /// Transport type
    DiskTransport transport{DiskTransport::Unknown};
    /// Whether the disk is an SSD
    bool is_ssd{false};
    /// Whether the disk is removable
    bool is_removable{false};
    /// Partition table type
    std::optional<std::string> pttype;
    /// List of partitions on this disk
    std::vector<PartitionInfo> partitions;
};

/// @brief Convert disk transport enum to string representation
/// @param transport The transport type to convert
/// @return string view of the transport type
auto disk_transport_to_string(DiskTransport transport) noexcept -> std::string_view;

/// @brief Convert transport string to disk transport enum
/// @param transport_str The transport string
/// @return disk transport enum value
auto string_to_disk_transport(std::string_view transport_str) noexcept -> DiskTransport;

/// @brief Lists all disk devices (excluding partitions and virtual devices)
/// @return Optional vector of DiskInfo, std::nullopt on failure
auto list_disks() noexcept -> std::optional<std::vector<DiskInfo>>;

/// @brief Gets detailed information for a specific disk
/// @param device The device path
/// @return optional DiskInfo, std::nullopt if device not found or error
auto get_disk_info(std::string_view device) noexcept -> std::optional<DiskInfo>;

/// @brief Lists partitions on a specific disk
/// @param disk_device The disk device path
/// @return vector of PartitionInfo objects
auto list_partitions(std::string_view disk_device) noexcept -> std::vector<PartitionInfo>;

/// @brief Converts disk partition info to partition schema format for editing
/// @param disk_device The disk device path
/// @return vector of fs::Partition objects suitable for schema editing
auto get_partition_schema(std::string_view disk_device) noexcept -> std::vector<fs::Partition>;

/// @brief Formats a size in bytes to human-readable string
/// @param bytes Size in bytes
/// @return Human-readable size string
auto format_size(std::uint64_t bytes) noexcept -> std::string;

/// @brief Determines if a storage device is an SSD
/// @param device The device path
/// @return True if the device is non-rotational (SSD/NVMe), false otherwise
auto is_device_ssd(std::string_view device) noexcept -> bool;

/// @brief Parses JSON output from lsblk command into DiskInfo structures
/// @param json_output The JSON string from lsblk -J command
/// @return vector of DiskInfo, empty on error
auto parse_lsblk_disks_json(std::string_view json_output) noexcept -> std::vector<DiskInfo>;

/// @brief Parses partition number of device
/// @param device The device path
/// @return extracted partition number
auto parse_partition_number(std::string_view device) noexcept -> std::uint32_t;

/// @brief Parses disk name(base) from device
/// @param device The device path
/// @return extracted disk name of the device
auto get_disk_name_from_device(std::string_view device) noexcept -> std::string_view;

}  // namespace gucc::disk

#endif  // SYSTEM_QUERY_HPP
