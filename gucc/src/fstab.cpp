#include "gucc/fstab.hpp"
#include "gucc/io_utils.hpp"

#include <cctype>  // for tolower

#include <algorithm>  // for transform
#include <filesystem>
#include <fstream>

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <range/v3/algorithm/transform.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace {
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

auto string_tolower(std::string_view text) noexcept -> std::string {
    std::string res{text};
    ranges::transform(res, res.begin(),
        [](char char_elem) { return static_cast<char>(std::tolower(static_cast<unsigned char>(char_elem))); });
    return res;
}

}  // namespace

namespace gucc::fs {

auto gen_fstab_entry(const Partition& partition) noexcept -> std::optional<std::string> {
    // Apperently some FS names named differently in /etc/fstab.
    const auto& fstype = [](auto&& fstype) -> std::string {
        auto lower_fstype = string_tolower(fstype);
        return std::string{convert_fsname_fstab(lower_fstype)};
    }(partition.fstype);

    const auto& luks_mapper_name = partition.luks_mapper_name;
    const auto& mountpoint       = partition.mountpoint;
    const auto& disk_name        = ::fs::path{partition.device}.filename().string();

    // NOTE: ignore swap support for now
    if (fstype == "swap"sv) {
        spdlog::info("fstab: skipping swap partition");
        return std::nullopt;
    }

    const auto check_num = [](auto&& mountpoint, auto&& fstype) -> std::int32_t {
        if (mountpoint == "/"sv && fstype != "btrfs"sv) {
            return 1;
        } else if (mountpoint != "swap"sv && fstype != "btrfs"sv) {
            return 2;
        }
        return 0;
    }(mountpoint, fstype);

    const auto& mount_options = [](auto&& mount_opts, auto&& subvolume, auto&& fstype) -> std::string {
        if (fstype == "btrfs"sv && !subvolume.empty()) {
            return fmt::format(FMT_COMPILE("subvol={},{}"), subvolume, mount_opts);
        }
        return {mount_opts};
    }(partition.mount_opts, partition.subvolume.value_or(""), fstype);

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

    std::ofstream fstab_file{fstab_filepath, std::ios::out | std::ios::trunc};
    if (!fstab_file.is_open()) {
        spdlog::error("Failed to open fstab for writing {}", fstab_filepath);
        return false;
    }
    fstab_file << fs::generate_fstab_content(partitions);
    return true;
}

}  // namespace gucc::fs
