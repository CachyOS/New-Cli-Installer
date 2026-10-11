#include "gucc/btrfs.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/partition.hpp"
#include "gucc/ranges_utils.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>   // for find_if, sort
#include <functional>  // for bind_back
#include <optional>    // for optional
#include <ranges>      // for ranges::*

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

// same behaviour as os.path.dirname from python
constexpr auto get_dirname(std::string_view full_path) noexcept -> std::string_view {
    if (full_path == "/"sv) {
        return full_path;
    }
    auto pos = full_path.find_last_of('/');
    if (pos == std::string_view::npos) {
        return {};
    }
    return full_path.substr(0, pos);
}

constexpr auto find_partition(const gucc::fs::Partition& part, const gucc::fs::BtrfsSubvolume& subvol) noexcept -> bool {
    return (part.mountpoint == subvol.mountpoint) || (part.subvolume && *part.subvolume == subvol.subvolume);
}

}  // namespace

namespace gucc::fs {

auto btrfs_create_subvol(std::string_view subvolume, std::string_view root_mountpoint) noexcept -> Result<void> {
    const auto& subvol_dirs_path = fmt::format(FMT_COMPILE("{}{}"), root_mountpoint, get_dirname(subvolume));
    if (auto res = file_utils::create_directories(subvol_dirs_path); !res) {
        return res;
    }
    const auto& cmd = fmt::format(FMT_COMPILE("btrfs subvolume create {}{}"), root_mountpoint, subvolume);
    return utils::exec_or_error(cmd, "create btrfs subvolume"sv);
}

auto btrfs_create_subvols(const std::vector<BtrfsSubvolume>& subvols, std::string_view device, std::string_view root_mountpoint, std::string_view mount_opts) noexcept -> Result<void> {
    // Create subvolumes
    for (const auto& subvol : subvols) {
        if (subvol.subvolume.empty()) {
            continue;
        }
        if (auto res = fs::btrfs_create_subvol(subvol.subvolume, root_mountpoint); !res) {
            return res;
        }
    }
    if (!utils::exec_checked(fmt::format(FMT_COMPILE("umount -v {}"), root_mountpoint))) {
        spdlog::warn("Failed to umount {} before mounting btrfs subvolumes", root_mountpoint);
    }

    return fs::btrfs_mount_subvols(subvols, device, root_mountpoint, mount_opts);
}

auto btrfs_mount_subvols(const std::vector<BtrfsSubvolume>& subvols, std::string_view device, std::string_view root_mountpoint, std::string_view mount_opts) noexcept -> Result<void> {
    for (const auto& subvol : subvols) {
        auto mount_option = fmt::format(FMT_COMPILE("subvol={},{}"), subvol.subvolume, mount_opts);
        if (subvol.subvolume.empty()) {
            mount_option = mount_opts;
        }

        // mount at the actual mountpoint where subvolume is going to be mounted after install
        const auto& subvolume_mountpoint = fmt::format(FMT_COMPILE("{}{}"), root_mountpoint, subvol.mountpoint);

        // TODO(vnepogodin): refactor create dir and mount into own function
        if (auto res = file_utils::create_directories(subvolume_mountpoint); !res) {
            return res;
        }

        // now mount subvolume
        const auto& mount_cmd = fmt::format(FMT_COMPILE("mount -o {} \"{}\" {}"), mount_option, device, subvolume_mountpoint);

        spdlog::debug("mounting..: {}", mount_cmd);
        if (auto res = utils::exec_or_error(mount_cmd, "mount btrfs subvolume"sv); !res) {
            return res;
        }
    }
    return {};
}

auto btrfs_append_subvolumes(std::vector<Partition>& partitions, const std::vector<BtrfsSubvolume>& subvols) noexcept -> Result<void> {
    // if the list of subvolumes is empty, just return success at the beginning of the function
    if (subvols.empty()) {
        return {};
    }

    // NOTE: that's a copy, no need to care
    const auto root_part = utils::find_if_optional(partitions, std::bind_back(is_root_on, "btrfs"sv));
    if (!root_part) {
        return make_error(ErrorCode::NotFound, "Unable to find root btrfs partition!");
    }

    for (auto&& subvol : subvols) {
        // check if we already have a partition with such subvolume
        auto part_it = std::ranges::find_if(partitions,
            [&subvol](auto&& part) { return find_partition(part, subvol); });

        // we have found it. proceed to overwrite the values
        if (part_it != std::ranges::end(partitions)) {
            part_it->mountpoint = subvol.mountpoint;
            part_it->subvolume  = std::make_optional<std::string>(subvol.subvolume);
            continue;
        }
        // otherwise, insert the partition based on the root partition
        Partition part{};
        part.fstype           = root_part->fstype;
        part.mountpoint       = subvol.mountpoint;
        part.uuid_str         = root_part->uuid_str;
        part.device           = root_part->device;
        part.mount_opts       = root_part->mount_opts;
        part.subvolume        = std::make_optional<std::string>(subvol.subvolume);
        part.luks_mapper_name = root_part->luks_mapper_name;
        part.luks_uuid        = root_part->luks_uuid;
        part.luks_passphrase  = root_part->luks_passphrase;
        partitions.emplace_back(std::move(part));
    }

    // sort by device
    std::ranges::sort(partitions, {}, &Partition::device);
    return {};
}

auto create_btrfs_installation_snapshot(std::string_view root_mountpoint) noexcept -> Result<void> {
    if (!utils::arch_chroot_query_checked("pacman -Q cachyos-snapper-support"sv, root_mountpoint)) {
        spdlog::info("cachyos-snapper-support not installed, skipping");
        return {};
    }

    if (auto res = file_utils::create_directories(fmt::format(FMT_COMPILE("{}/etc/cachyos"), root_mountpoint)); !res) {
        return res;
    }

    // create a permanent snapper snapshot of the finished install
    const auto snapshot_id = utils::arch_chroot_output(
        R"(snapper --no-dbus -c root create --print-number --description 'Fresh CachyOS Installation' --userdata 'important=yes')"sv, root_mountpoint);
    if (!snapshot_id) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to create btrfs installation snapshot on {}", root_mountpoint));
    }

    // save the record id for easy restore
    const auto& snap_file = fmt::format(FMT_COMPILE("{}/etc/cachyos/installation-snapshot"), root_mountpoint);
    return file_utils::write_file(snap_file, fmt::format(FMT_COMPILE("{}\n"), utils::trim(*snapshot_id)));
}

}  // namespace gucc::fs
