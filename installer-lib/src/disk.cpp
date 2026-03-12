#include "cachyos/disk.hpp"

#include <expected>     // for unexpected
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <spdlog/spdlog.h>

namespace cachyos::installer {

auto default_btrfs_subvolumes() noexcept -> std::vector<gucc::fs::BtrfsSubvolume> {
    return {};
}

auto get_available_mount_opts(std::string_view /*fstype*/) noexcept -> std::vector<std::string> {
    return {};
}

auto is_volume_removable(std::string_view /*device*/) noexcept -> bool {
    return false;
}

auto auto_partition(std::string_view /*device*/, std::string_view /*system_mode*/,
    gucc::bootloader::BootloaderType /*bootloader*/, const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<std::vector<gucc::fs::Partition>, std::string> {
    return std::unexpected("not yet implemented");
}

auto apply_mount_selections(const MountSelections& /*selections*/,
    std::string_view /*mountpoint*/) noexcept
    -> std::expected<std::vector<gucc::fs::Partition>, std::string> {
    return std::unexpected("not yet implemented");
}

auto apply_btrfs_subvolumes(const std::vector<gucc::fs::BtrfsSubvolume>& /*subvols*/,
    const RootPartitionSelection& /*selection*/,
    std::string_view /*mountpoint*/,
    std::vector<gucc::fs::Partition>& /*partitions*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto setup_esp_partition(std::string_view /*device*/, std::string_view /*mountpoint*/,
    std::string_view /*root_mountpoint*/, bool /*format_requested*/) noexcept
    -> std::expected<gucc::fs::Partition, std::string> {
    return std::unexpected("not yet implemented");
}

auto umount_partitions(std::string_view /*mountpoint*/,
    const std::vector<std::string>& /*zfs_pools*/,
    std::string_view /*swap_device*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto generate_fstab(std::string_view /*mountpoint*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

}  // namespace cachyos::installer
