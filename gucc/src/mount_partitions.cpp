#include "gucc/mount_partitions.hpp"
#include "gucc/block_devices.hpp"
#include "gucc/crypto_detection.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/string_utils.hpp"

#include <memory>  // for unique_ptr
#include <string>  // for string
#include <vector>  // for vector

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace gucc::mount {

auto build_mount_argv(std::string_view partition, std::string_view mount_dir, std::string_view mount_opts, std::string_view fstype) noexcept -> std::vector<std::string> {
    std::vector<std::string> argv{"/usr/bin/mount"s};
    if (!fstype.empty()) {
        argv.insert(argv.end(), {"-t"s, std::string{fstype}});
    }
    if (!mount_opts.empty()) {
        argv.insert(argv.end(), {"-o"s, std::string{mount_opts}});
    }
    argv.insert(argv.end(), {std::string{partition}, std::string{mount_dir}});
    return argv;
}

auto mount_partition(std::string_view partition, std::string_view mount_dir, std::string_view mount_opts, std::string_view fstype) noexcept -> Result<void> {
    return utils::exec_or_error(build_mount_argv(partition, mount_dir, mount_opts, fstype), "mount partition"sv);
}

auto umount(std::string_view mountpoint, bool lazy) noexcept -> Result<void> {
    std::vector<std::string> argv{"/usr/bin/umount"s, "-v"s};
    if (lazy) {
        argv.emplace_back("-l"s);
    }
    argv.emplace_back(mountpoint);
    return utils::exec_or_error(argv, "unmount"sv);
}

auto bind_mount(std::string_view source, std::string_view target) noexcept -> Result<void> {
    return utils::exec_or_error({"/usr/bin/mount"s, "--bind"s, std::string{source}, std::string{target}}, "bind mount"sv);
}

auto query_partition(std::string_view partition, std::int32_t& is_luks, std::int32_t& is_lvm, std::string& luks_name, std::string& luks_dev, std::string& luks_uuid) noexcept -> Result<void> {
    const auto& devices = disk::list_block_devices();
    if (!devices) {
        return make_error(ErrorCode::NotFound, "failed to list block devices");
    }

    // Identify if mounted partition is crypto
    const auto& crypto = disk::detect_crypto_for_device(*devices, partition);
    if (!crypto || !crypto->is_luks) {
        return make_error(ErrorCode::NotFound, FMT_COMPILE("partition {} is not LUKS"), partition);
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
    return {};
}

auto setup_esp_partition(std::string_view device, std::string_view mountpoint, std::string_view base_mountpoint, bool format, bool is_ssd) noexcept -> Result<fs::Partition> {
    const auto& full_mountpoint = utils::target_path(base_mountpoint, mountpoint);

    // Format esp part if requested
    if (format) {
        if (auto res = fs::format_partition(device, fs::FilesystemType::Vfat); !res) {
            return std::unexpected(std::move(res.error()));
        }
        spdlog::info("Formatted ESP partition {} with {}", device, fs::get_mkfs_command(fs::FilesystemType::Vfat));
        // let blkid see the fresh vfat before we mount it
        utils::settle_devices();
    }

    if (auto res = file_utils::create_directories(full_mountpoint); !res) {
        return std::unexpected(std::move(res.error()));
    }

    // Mount the partition. boot is always vfat so hardcode as so
    const auto& boot_opts = fs::get_default_mount_opts(fs::FilesystemType::Vfat, is_ssd);
    if (auto res = mount_partition(device, full_mountpoint, boot_opts, "vfat"sv); !res) {
        return std::unexpected(std::move(res.error()));
    }

    auto [part_fs, part_uuid] = fs::utils::get_device_info(device);
    return fs::Partition{
        .fstype     = part_fs.empty() ? "vfat"s : std::move(part_fs),
        .mountpoint = std::string{mountpoint},
        .uuid_str   = std::move(part_uuid),
        .device     = std::string{device},
        .mount_opts = boot_opts,
    };
}

auto ScopedMount::mount(std::string_view partition, std::string_view mount_dir, std::string_view mount_opts, std::string_view fstype) noexcept -> Result<ScopedMount> {
    return mount_partition(partition, mount_dir, mount_opts, fstype).transform([mount_dir] { return ScopedMount{mount_dir}; });
}

auto ScopedMount::bind(std::string_view source, std::string_view target) noexcept -> Result<ScopedMount> {
    return bind_mount(source, target).transform([target] { return ScopedMount{target}; });
}

void ScopedMount::Unmount::operator()(std::string* path) const noexcept {
    // well well well
    const std::unique_ptr<std::string> owned{path};
    if (auto res = umount(*owned); !res) {
        spdlog::warn("Failed to unmount {}: {}", *owned, to_string(res.error()));
    }
}

}  // namespace gucc::mount
