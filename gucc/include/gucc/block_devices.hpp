#ifndef BLOCK_DEVICES_HPP
#define BLOCK_DEVICES_HPP

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
    /// Mount point.
    std::optional<std::string> mountpoint;
    /// Parent device name.
    std::optional<std::string> pkname;
    /// Filesystem label.
    std::optional<std::string> label;
    /// Partition UUID.
    std::optional<std::string> partuuid;
    /// Size of the device in bytes.
    std::optional<std::uint64_t> size;

    /// @brief Default constructor for BlockDevice.
    BlockDevice() = default;
};

/// @brief Executes lsblk command and returns a list of block devices.
/// @return An optional vector of BlockDevice objects, std::nullopt otherwise.
auto list_block_devices() -> std::optional<std::vector<BlockDevice>>;

/// @brief Finds a block device by its name.
/// @param devices A vector of BlockDevice objects.
/// @param device_name A string view containing the name of the device to find.
/// @return An optional BlockDevice object if found, std::nullopt otherwise.
auto find_device_by_name(const std::vector<BlockDevice>& devices, std::string_view device_name) -> std::optional<BlockDevice>;

}  // namespace gucc::disk

#endif  // BLOCK_DEVICES_HPP
