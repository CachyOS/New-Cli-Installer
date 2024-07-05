#include "gucc/umount_partitions.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/mtab.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>  // for sort

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <range/v3/algorithm/sort.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

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
    ranges::sort(*mtab_entries, {}, &mtab::MTabEntry::mountpoint);

    spdlog::debug("Got {} entries from mountpoint {}", mtab_entries->size(), root_mountpoint);
    for (auto&& mtab_entry : std::move(*mtab_entries)) {
        const auto& umount_cmd = fmt::format(FMT_COMPILE("umount -v {} &>>/tmp/cachyos-install.log"), mtab_entry.mountpoint);
        if (utils::exec(umount_cmd, true) != "0") {
            spdlog::error("Failed to umount partition: {} {}", mtab_entry.device, mtab_entry.mountpoint);
            return false;
        }
    }

    for (auto&& zfs_poolname : zfs_poolnames) {
        const auto& zpool_export_cmd = fmt::format(FMT_COMPILE("zpool export {} &>>/tmp/cachyos-install.log"), zfs_poolname);
        if (utils::exec(zpool_export_cmd, true) != "0") {
            spdlog::error("Failed to export zpool: {}", zfs_poolname);
            return false;
        }
    }

    return true;
}

}  // namespace gucc::umount
