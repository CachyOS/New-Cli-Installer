#include "gucc/mount_partitions.hpp"
#include "gucc/block_devices.hpp"
#include "gucc/crypto_detection.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/string_utils.hpp"

#include <filesystem>  // for create_directories

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace gucc::mount {

auto mount_partition(std::string_view partition, std::string_view mount_dir, std::string_view mount_opts) noexcept -> bool {
    if (!mount_opts.empty()) {
        return utils::exec_checked(fmt::format(FMT_COMPILE("mount -o {} {} {}"), mount_opts, partition, mount_dir));
    }
    return utils::exec_checked(fmt::format(FMT_COMPILE("mount {} {}"), partition, mount_dir));
}

auto query_partition(std::string_view partition, std::int32_t& is_luks, std::int32_t& is_lvm, std::string& luks_name, std::string& luks_dev, std::string& luks_uuid) noexcept -> bool {
    const auto& devices = disk::list_block_devices();
    if (!devices) {
        return false;
    }

    // Identify if mounted partition is crypto
    const auto& crypto = disk::detect_crypto_for_device(*devices, partition);
    if (!crypto || !crypto->is_luks) {
        return false;
    }

    is_luks   = true;
    luks_name = crypto->luks_mapper_name;
    luks_uuid = crypto->luks_uuid;
    if (crypto->is_lvm) {
        is_lvm = true;
    }
    if (!crypto->luks_dev.empty()) {
        luks_dev = fmt::format(FMT_COMPILE("{} {}"), luks_dev, crypto->luks_dev);
    }
    return true;
}

auto setup_esp_partition(std::string_view device, std::string_view mountpoint, std::string_view base_mountpoint, bool format, bool is_ssd) noexcept -> std::optional<fs::Partition> {
    const auto& full_mountpoint = fmt::format(FMT_COMPILE("{}{}"), base_mountpoint, mountpoint);

    // Format esp part if requested
    if (format) {
        const auto& mkfs_cmd = fs::get_mkfs_command(fs::FilesystemType::Vfat);
        if (!utils::exec_checked(fmt::format(FMT_COMPILE("{} {} &>/dev/null"), mkfs_cmd, device))) {
            spdlog::error("Failed to format ESP partition {} with {}", device, mkfs_cmd);
            return std::nullopt;
        }
        spdlog::info("Formatted ESP partition {} with {}", device, mkfs_cmd);
    }

    // Create mount directory
    std::error_code err{};
    std::filesystem::create_directories(full_mountpoint, err);
    if (err) {
        spdlog::error("Failed to create ESP directory {}: {}", full_mountpoint, err.message());
        return std::nullopt;
    }

    // Mount the partition
    const auto& boot_opts = fs::get_default_mount_opts(fs::FilesystemType::Vfat, is_ssd);
    if (!mount_partition(device, full_mountpoint, boot_opts)) {
        spdlog::error("Failed to mount ESP {} at {}", device, full_mountpoint);
        return std::nullopt;
    }

    // Construct Partition struct
    const auto& part_fs   = fs::utils::get_mountpoint_fs(full_mountpoint);
    const auto& part_uuid = fs::utils::get_device_uuid(device);

    return fs::Partition{
        .fstype     = part_fs.empty() ? "vfat"s : part_fs,
        .mountpoint = std::string{mountpoint},
        .uuid_str   = part_uuid,
        .device     = std::string{device},
        .mount_opts = boot_opts,
    };
}

}  // namespace gucc::mount
