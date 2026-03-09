#include "gucc/mount_partitions.hpp"
#include "gucc/block_devices.hpp"
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
        return true;
    }

    const auto& dev = disk::find_device_by_name(*devices, partition);
    if (!dev) {
        return true;
    }

    // Identify if mounted partition is type "crypt" (LUKS on LVM, or LUKS alone)
    const bool is_crypt = (dev->type == "crypt") || disk::has_type_in_ancestry(*devices, dev->name, "crypt"sv);
    if (!is_crypt) {
        return true;
    }

    is_luks   = true;
    luks_name = std::string{disk::strip_device_prefix(partition)};

    // Check if LUKS on LVM (lvm device with crypto_LUKS fstype)
    const auto& luks_on_lvm = disk::find_devices_by_type_and_fstype(*devices, "lvm"sv, "crypto_LUKS"sv);
    if (!luks_on_lvm.empty()) {
        for (const auto& cryptpart : luks_on_lvm) {
            luks_dev = fmt::format(FMT_COMPILE("{} cryptdevice={}:{}"), luks_dev, cryptpart.name, luks_name);
            is_lvm   = true;
        }
        return true;
    }

    // Check if LVM on LUKS (crypt device with LVM2_member fstype)
    const auto& lvm_on_luks = disk::find_devices_by_type_and_fstype(*devices, "crypt"sv, "LVM2_member"sv);
    if (!lvm_on_luks.empty()) {
        for (const auto& cryptpart : lvm_on_luks) {
            luks_dev = fmt::format(FMT_COMPILE("{} cryptdevice={}:{}"), luks_dev, cryptpart.name, luks_name);
            is_lvm   = true;
        }
        return true;
    }

    // Check if LUKS alone (parent = part with crypto_LUKS fstype)
    const auto& luks_parts = disk::find_devices_by_type_and_fstype(*devices, "part"sv, "crypto_LUKS"sv);
    if (!luks_parts.empty()) {
        luks_uuid = luks_parts.front().uuid;
        luks_dev  = fmt::format(FMT_COMPILE("{} cryptdevice=UUID={}:{}"), luks_dev, luks_uuid, luks_name);
        return true;
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
