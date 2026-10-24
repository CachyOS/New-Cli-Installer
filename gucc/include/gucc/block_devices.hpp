#ifndef BLOCK_DEVICES_HPP
#define BLOCK_DEVICES_HPP

#include "gucc/string_utils.hpp"

#include <cstdint>      // for uint64_t
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::disk {

/// @brief Represents a block device with its properties.
struct BlockDevice {
    /// Device name (e.g., /dev/nvme0n1).
    std::string name;
    /// Device type (e.g., part, crypt, disk).
    std::string type;
    /// Filesystem type (e.g., ext4, btrfs).
    std::string fstype;
    /// Device UUID.
    std::string uuid;
    /// Device model.
    std::optional<std::string> model;
    /// Mount points (util-linux >= 2.37 exposes MOUNTPOINTS as array for
    /// devices with multiple mounts, e.g. btrfs subvolumes).
    std::vector<std::string> mountpoints;
    /// Parent device name.
    std::optional<std::string> pkname;
    /// Filesystem label.
    std::optional<std::string> label;
    /// Partition UUID.
    std::optional<std::string> partuuid;
    /// Size of the device in bytes.
    std::optional<std::uint64_t> size;
    /// Partition table type (e.g. gpt, dos), disks only.
    std::optional<std::string> pttype;
    /// Transport (e.g. nvme, sata, usb), empty for virtual disks.
    std::string transport;
    /// Removable media.
    bool removable{false};

    /// @brief Default constructor for BlockDevice.
    BlockDevice() = default;
};

/// @brief Executes lsblk command and returns a list of block devices.
/// @return An optional vector of BlockDevice objects, std::nullopt otherwise.
auto list_block_devices() -> std::optional<std::vector<BlockDevice>>;

/// lsblk for @p device without children, std::nullopt otherwise.
auto query_block_device(std::string_view device) -> std::optional<BlockDevice>;

/// @brief Finds a block device by its name.
/// @param devices A vector of BlockDevice objects.
/// @param device_name A string view containing the name of the device to find.
/// @return An optional BlockDevice object if found, std::nullopt otherwise.
auto find_device_by_name(const std::vector<BlockDevice>& devices, std::string_view device_name) -> std::optional<BlockDevice>;

/// @brief Finds a block device by matching its parent kernel name (pkname).
/// @param devices A vector of BlockDevice objects.
/// @param pkname A string view representing the parent kernel name to search for.
/// @return An optional BlockDevice object if found, std::nullopt otherwise.
auto find_device_by_pkname(const std::vector<BlockDevice>& devices, std::string_view pkname) -> std::optional<BlockDevice>;

/// @brief Finds a block device by its mountpoint.
/// @param devices A vector of BlockDevice objects.
/// @param mountpoint The mountpoint to search for.
/// @return An optional BlockDevice object if found, std::nullopt otherwise.
auto find_device_by_mountpoint(const std::vector<BlockDevice>& devices, std::string_view mountpoint) -> std::optional<BlockDevice>;

/// @brief Checks if a device has an ancestor of the given type by walking the pkname chain.
/// @param devices A vector of BlockDevice objects.
/// @param device_name The device name to start from.
/// @param type The type to search for in ancestry.
/// @return True if any ancestor has the given type.
auto has_type_in_ancestry(const std::vector<BlockDevice>& devices, std::string_view device_name, std::string_view type) -> bool;

/// @brief Finds the first ancestor of the given type by walking the pkname chain.
/// @param devices A vector of BlockDevice objects.
/// @param device_name The device name to start from.
/// @param type The type to search for.
/// @return An optional BlockDevice of the matching ancestor, std::nullopt otherwise.
auto find_ancestor_of_type(const std::vector<BlockDevice>& devices, std::string_view device_name, std::string_view type) -> std::optional<BlockDevice>;

/// @brief Returns all devices matching the given type.
/// @param devices A vector of BlockDevice objects.
/// @param type The device type to filter by.
/// @return A vector of matching BlockDevice objects.
auto find_devices_by_type(const std::vector<BlockDevice>& devices, std::string_view type) -> std::vector<BlockDevice>;

inline constexpr std::string_view kDevPrefix{"/dev/"};
inline constexpr std::string_view kDevMapperPrefix{"/dev/mapper/"};

/// @brief Strips /dev/mapper/ or /dev/ prefix from a device path
/// @param device The device path (e.g., /dev/mapper/cryptroot or /dev/sda1)
/// @return The device name without the prefix (e.g., cryptroot or sda1)
constexpr auto strip_device_prefix(std::string_view device) noexcept -> std::string_view {
    return utils::strip_prefix(device, kDevMapperPrefix)
        .or_else([device] { return utils::strip_prefix(device, kDevPrefix); })
        .value_or(device);
}

/// Device node of the device-mapper target @p name, the inverse of strip_device_prefix.
[[nodiscard]] constexpr auto mapper_path(std::string_view name) -> std::string {
    return std::string{kDevMapperPrefix}.append(name);
}

/// @brief Returns all devices matching both type and fstype.
/// @param devices A vector of BlockDevice objects.
/// @param type The device type to filter by.
/// @param fstype The filesystem type to filter by (case-insensitive).
/// @return A vector of matching BlockDevice objects.
auto find_devices_by_type_and_fstype(const std::vector<BlockDevice>& devices, std::string_view type, std::string_view fstype) -> std::vector<BlockDevice>;

}  // namespace gucc::disk

namespace gucc::disk::detail {

/// Common lsblk parsed fields.
inline constexpr std::string_view kLsblkColumns{"NAME,TYPE,FSTYPE,UUID,PARTUUID,PKNAME,LABEL,SIZE,MOUNTPOINTS,MODEL,PTTYPE,RM,TRAN"};

auto parse_lsblk_json(std::string_view json_output) noexcept -> std::optional<std::vector<BlockDevice>>;

}  // namespace gucc::disk::detail

#endif  // BLOCK_DEVICES_HPP
