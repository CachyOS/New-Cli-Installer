#include "gucc/umount_partitions.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/mtab.hpp"

#include <algorithm>  // for sort
#include <ranges>     // for ranges::*

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::umount {

auto umount_partitions(std::string_view root_mountpoint, const std::vector<std::string>& zfs_poolnames) noexcept -> bool {
    auto mtab_entries = mtab::parse_mtab(root_mountpoint);
    if (!mtab_entries) {
        spdlog::error("Failed to umount partitions: failed to parse /etc/mtab");
        return false;
    }
    std::ranges::sort(*mtab_entries, {}, &mtab::MTabEntry::mountpoint);

    spdlog::debug("Got {} entries from mountpoint {}", mtab_entries->size(), root_mountpoint);
    for (auto&& mtab_entry : std::move(*mtab_entries)) {
        const auto& umount_cmd = fmt::format(FMT_COMPILE("umount -v {} &>>/tmp/cachyos-install.log"), mtab_entry.mountpoint);
        if (!utils::exec_checked(umount_cmd)) {
            spdlog::error("Failed to umount partition: {} {}", mtab_entry.device, mtab_entry.mountpoint);
            return false;
        }
    }

    for (auto&& zfs_poolname : zfs_poolnames) {
        const auto& zpool_export_cmd = fmt::format(FMT_COMPILE("zpool export {} &>>/tmp/cachyos-install.log"), zfs_poolname);
        if (!utils::exec_checked(zpool_export_cmd)) {
            spdlog::error("Failed to export zpool: {}", zfs_poolname);
            return false;
        }
    }

    return true;
}

}  // namespace gucc::umount
