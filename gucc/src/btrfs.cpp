#include "gucc/btrfs.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/partition.hpp"

#include <algorithm>   // for find_if
#include <filesystem>  // for create_directories
#include <optional>    // for optional
#include <ranges>      // for ranges::*
#include <utility>     // for make_optional

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
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

constexpr auto is_root_btrfs_part(const gucc::fs::Partition& part) noexcept -> bool {
    return (part.mountpoint == "/"sv) && (part.fstype == "btrfs"sv);
}

constexpr auto find_root_btrfs_part(auto&& parts) noexcept {
    return std::ranges::find_if(parts,
        [](auto&& part) { return is_root_btrfs_part(part); });
}

}  // namespace

namespace gucc::fs {

auto btrfs_create_subvol(std::string_view subvolume, std::string_view root_mountpoint) noexcept -> bool {
    const auto& subvol_dirs_path = fmt::format(FMT_COMPILE("{}{}"), root_mountpoint, get_dirname(subvolume));
    std::error_code err{};
    ::fs::create_directories(subvol_dirs_path, err);
    if (err) {
        spdlog::error("Failed to create directories for btrfs subvolume {}: {}", subvol_dirs_path, err.message());
        return false;
    }
    auto cmd = fmt::format(FMT_COMPILE("btrfs subvolume create {}{} &>>/tmp/cachyos-install.log"), root_mountpoint, subvolume);
    return utils::exec_checked(cmd);
}

auto btrfs_create_subvols(const std::vector<BtrfsSubvolume>& subvols, std::string_view device, std::string_view root_mountpoint, std::string_view mount_opts) noexcept -> bool {
    // Create subvolumes
    for (const auto& subvol : subvols) {
        if (subvol.subvolume.empty()) {
            continue;
        }
        if (!fs::btrfs_create_subvol(subvol.subvolume, root_mountpoint)) {
            spdlog::error("Failed to create btrfs subvolume {} on root mountpoint {}", subvol.subvolume, root_mountpoint);
            return false;
        }
    }
    // TODO(vnepogodin): handle exit code
    utils::exec(fmt::format(FMT_COMPILE("umount -v {} &>>/tmp/cachyos-install.log"), root_mountpoint), true);

    // Mount subvolumes
    if (!fs::btrfs_mount_subvols(subvols, device, root_mountpoint, mount_opts)) {
        spdlog::error("Failed to mount btrfs subvolumes");
        return false;
    }
    return true;
}

auto btrfs_mount_subvols(const std::vector<BtrfsSubvolume>& subvols, std::string_view device, std::string_view root_mountpoint, std::string_view mount_opts) noexcept -> bool {
    for (const auto& subvol : subvols) {
        auto mount_option = fmt::format(FMT_COMPILE("subvol={},{}"), subvol.subvolume, mount_opts);
        if (subvol.subvolume.empty()) {
            mount_option = mount_opts;
        }

        // mount at the actual mountpoint where subvolume is going to be mounted after install
        const auto& subvolume_mountpoint = fmt::format(FMT_COMPILE("{}{}"), root_mountpoint, subvol.mountpoint);

        // TODO(vnepogodin): refactor create dir and mount into own function
        std::error_code err{};
        ::fs::create_directories(subvolume_mountpoint, err);
        if (err) {
            spdlog::error("Failed to create directories for btrfs subvols mountpoint {}: {}", subvolume_mountpoint, err.message());
            return false;
        }

        // now mount subvolume
        const auto& mount_cmd = fmt::format(FMT_COMPILE("mount -o {} \"{}\" {}"), mount_option, device, subvolume_mountpoint);

        spdlog::debug("mounting..: {}", mount_cmd);
        if (!utils::exec_checked(mount_cmd)) {
            spdlog::error("Failed to mount subvolume {} mountpoint {} with: {}", subvol.subvolume, subvolume_mountpoint, mount_cmd);
            return false;
        }
    }
    return true;
}

auto btrfs_append_subvolumes(std::vector<Partition>& partitions, const std::vector<BtrfsSubvolume>& subvols) noexcept -> bool {
    // if the list of subvolumes is empty, just return success at the beginning of the function
    if (subvols.empty()) {
        return true;
    }

    auto root_part_it = find_root_btrfs_part(partitions);
    if (root_part_it == std::ranges::end(partitions)) {
        spdlog::error("Unable to find root btrfs partition!");
        return false;
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
        // overwise, let's insert the partition based on the root partition

        // NOTE: we don't need to check for root part here,
        // because it was already checked in the beginning
        root_part_it = find_root_btrfs_part(partitions);

        Partition part{};
        part.fstype           = root_part_it->fstype;
        part.mountpoint       = subvol.mountpoint;
        part.uuid_str         = root_part_it->uuid_str;
        part.device           = root_part_it->device;
        part.mount_opts       = root_part_it->mount_opts;
        part.subvolume        = std::make_optional<std::string>(subvol.subvolume);
        part.luks_mapper_name = root_part_it->luks_mapper_name;
        part.luks_uuid        = root_part_it->luks_uuid;
        part.luks_passphrase  = root_part_it->luks_passphrase;
        partitions.emplace_back(std::move(part));
    }

    // sort by device
    std::ranges::sort(partitions, {}, &Partition::device);
    return true;
}

}  // namespace gucc::fs
