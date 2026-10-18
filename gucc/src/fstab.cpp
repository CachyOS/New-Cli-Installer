#include "gucc/fstab.hpp"
#include "gucc/conf.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/zfs_query.hpp"

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

auto generate_fstab(const std::vector<Partition>& partitions, std::string_view root_mountpoint) noexcept -> Result<void> {
    const auto& fstab_filepath = fmt::format(FMT_COMPILE("{}/etc/fstab"), root_mountpoint);
    const auto& fstab_content  = fs::generate_fstab_content(partitions);
    return file_utils::write_file(fstab_filepath, fstab_content);
}

auto run_genfstab_on_mount(std::string_view root_mountpoint) noexcept -> Result<void> {
    const auto& fstab_filepath = fmt::format(FMT_COMPILE("{}/etc/fstab"), root_mountpoint);

    // run command to generate fstab
    const auto& fstab_cmd = fmt::format(FMT_COMPILE("genfstab -U -p {} > {}"), root_mountpoint, fstab_filepath);
    if (auto res = utils::exec_or_error(fstab_cmd, "generate fstab"sv); !res) {
        return res;
    }

    // dump generated fstab file into the log
    const auto& fstab_content = file_utils::read_whole_file(fstab_filepath);
    spdlog::info("Created fstab file:\n{}", fstab_content);

    // append swapfile in case it was detected
    auto content          = fstab_content;
    const auto& swap_file = fmt::format(FMT_COMPILE("{}/swapfile"), root_mountpoint);
    if (::fs::exists(swap_file) && ::fs::is_regular_file(swap_file)) {
        spdlog::info("Appending swapfile to the fstab..");
        content = detail::strip_root_prefix(content, root_mountpoint);
    }

    // Edit fstab in case of btrfs subvolumes
    content = detail::strip_subvolid_options(content);

    // remove any zfs datasets that are mounted by zfs
    const auto& zfs_datasets = list_zfs_datasets()
        | std::ranges::views::filter([](const ZfsDatasetInfo& ds) { return ds.mountpoint.starts_with('/'); })
        | std::ranges::views::transform(&ZfsDatasetInfo::name)
        | std::ranges::to<std::vector<std::string>>();
    if (!zfs_datasets.empty()) {
        content = detail::comment_out_zfs_entries(content, zfs_datasets);
    }

    if (content != fstab_content && !file_utils::write_file(fstab_filepath, content)) {
        spdlog::warn("Failed to post-process {}", fstab_filepath);
    }
    return {};
}

}  // namespace gucc::fs

namespace gucc::fs::detail {

auto comment_out_zfs_entries(std::string_view fstab_content, const std::vector<std::string>& zfs_datasets) noexcept -> std::string {
    return conf::comment_out_where(fstab_content, [&zfs_datasets](const conf::Line& line) {
        return std::ranges::contains(zfs_datasets, utils::first_field(line.body));
    });
}

auto strip_root_prefix(std::string_view fstab_content, std::string_view root_mountpoint) noexcept -> std::string {
    return utils::map_lines(fstab_content, [root_mountpoint](std::string_view line) {
        const auto pos = root_mountpoint.empty() ? std::string_view::npos : line.find(root_mountpoint);
        if (pos == std::string_view::npos) {
            return std::string{line};
        }
        return fmt::format(FMT_COMPILE("{}{}"), line.substr(0, pos), line.substr(pos + root_mountpoint.size()));
    });
}

auto strip_subvolid_options(std::string_view fstab_content) noexcept -> std::string {
    static constexpr auto kSubvolid = "subvolid="sv;
    static constexpr auto kSubvol   = ",subvol=/"sv;
    return utils::map_lines(fstab_content, [](std::string_view line) {
        // TODO(vnepogodin): ugly but let it be, position shouldn't even matter here
        const auto start  = line.find(kSubvolid);
        const auto subvol = start == std::string_view::npos ? start : line.find(kSubvol, start + kSubvolid.size());
        const auto end    = line.rfind(',');
        if (subvol == std::string_view::npos || end == std::string_view::npos || end < subvol + kSubvol.size()) {
            return std::string{line};
        }
        return fmt::format(FMT_COMPILE("{}{}"), line.substr(0, start), line.substr(end + 1));
    });
}

}  // namespace gucc::fs::detail
