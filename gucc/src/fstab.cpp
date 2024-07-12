#include "gucc/fstab.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"

#include <cctype>  // for tolower

#include <algorithm>   // for transform
#include <filesystem>  // for fs::path
#include <ranges>      // for ranges::transform

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace {

// NOLINTNEXTLINE
static constexpr auto FSTAB_HEADER = R"(# Static information about the filesystems.
# See fstab(5) for details.

# <file system> <dir> <type> <options> <dump> <pass>
)"sv;

constexpr auto convert_fsname_fstab(std::string_view fsname) noexcept -> std::string_view {
    if (fsname == "fat16"sv || fsname == "fat32"sv) {
        return "vfat"sv;
    } else if (fsname == "linuxswap"sv) {
        return "swap"sv;
    }
    return fsname;
}

constexpr auto get_check_number(std::string_view mountpoint, std::string_view fstype) noexcept -> std::int32_t {
    if (mountpoint == "/"sv && fstype != "btrfs"sv) {
        return 1;
    } else if (mountpoint != "swap"sv && fstype != "btrfs"sv) {
        return 2;
    }
    return 0;
}

constexpr auto get_mount_options(std::string_view mount_opts, std::string_view btrfs_subvolume, std::string_view fstype) noexcept -> std::string {
    if (fstype == "btrfs"sv && !btrfs_subvolume.empty()) {
        return fmt::format(FMT_COMPILE("subvol={},{}"), btrfs_subvolume, mount_opts);
    }
    return std::string{mount_opts};
}

constexpr auto string_tolower(std::string_view text) noexcept -> std::string {
    std::string res{text};
    std::ranges::transform(res, res.begin(),
        [](char char_elem) { return static_cast<char>(std::tolower(static_cast<unsigned char>(char_elem))); });
    return res;
}

constexpr auto get_converted_fs(std::string_view fstype) noexcept -> std::string {
    auto lower_fstype = string_tolower(fstype);
    return std::string{convert_fsname_fstab(lower_fstype)};
}

}  // namespace

namespace gucc::fs {

auto gen_fstab_entry(const Partition& partition) noexcept -> std::optional<std::string> {
    // Apparently some FS names named differently in /etc/fstab.
    const auto& fstype = get_converted_fs(partition.fstype);

    const auto& luks_mapper_name = partition.luks_mapper_name;
    const auto& mountpoint       = partition.mountpoint;
    const auto& disk_name        = ::fs::path{partition.device}.filename().string();

    // NOTE: ignore swap support for now
    if (fstype == "swap"sv) {
        spdlog::info("fstab: skipping swap partition");
        return std::nullopt;
    }

    const auto check_num      = get_check_number(mountpoint, fstype);
    const auto& mount_options = get_mount_options(partition.mount_opts, partition.subvolume.value_or(""), fstype);

    std::string device_str{partition.device};
    if (luks_mapper_name) {
        device_str = fmt::format(FMT_COMPILE("/dev/mapper/{}"), *luks_mapper_name);
    } else if (!partition.uuid_str.empty()) {
        device_str = fmt::format(FMT_COMPILE("UUID={}"), partition.uuid_str);
    }
    return std::make_optional<std::string>(fmt::format(FMT_COMPILE("# {}\n{:41} {:<14} {:<7} {:<10} 0 {}\n\n"), partition.device, device_str, mountpoint, fstype, mount_options, check_num));
}

auto generate_fstab_content(const std::vector<Partition>& partitions) noexcept -> std::string {
    std::string fstab_content{FSTAB_HEADER};

    for (auto&& partition : partitions) {
        if (partition.fstype == "zfs"sv) {
            // zfs partitions don't need an entry in fstab
            continue;
        }
        // if btrfs and root mountpoint
        /*if (partition.fstype == "btrfs"sv && partition.mountpoint == "/"sv) {
            // NOTE: should we handle differently here?
        }*/
        auto fstab_entry = gen_fstab_entry(partition);
        if (!fstab_entry) {
            continue;
        }
        fstab_content += std::move(*fstab_entry);
    }

    return fstab_content;
}

auto generate_fstab(const std::vector<Partition>& partitions, std::string_view root_mountpoint) noexcept -> bool {
    const auto& fstab_filepath = fmt::format(FMT_COMPILE("{}/etc/fstab"), root_mountpoint);
    const auto& fstab_content  = fs::generate_fstab_content(partitions);
    if (!file_utils::create_file_for_overwrite(fstab_filepath, fstab_content)) {
        spdlog::error("Failed to open fstab for writing {}", fstab_filepath);
        return false;
    }
    return true;
}

}  // namespace gucc::fs
