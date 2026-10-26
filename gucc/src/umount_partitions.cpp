#include "gucc/umount_partitions.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/mount_partitions.hpp"
#include "gucc/mtab.hpp"
#include "gucc/zfs.hpp"

#include <unistd.h>  // for sync

#include <algorithm>  // for sort
#include <ranges>     // for ranges::*

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::umount {

auto umount_partitions(std::string_view root_mountpoint, const std::vector<std::string>& zfs_poolnames) noexcept -> Result<void> {
    auto mtab_entries = mtab::parse_mtab(root_mountpoint);
    if (!mtab_entries) {
        return make_error(ErrorCode::ParseError, "Failed to umount partitions: failed to parse /etc/mtab");
    }

    // Sort in reverse (descending) order so deepest mountpoints are unmounted first.
    std::ranges::sort(*mtab_entries, std::ranges::greater(), &mtab::MTabEntry::mountpoint);

    // Flush filesystem buffers before unmounting
    ::sync();

    spdlog::debug("Got {} entries from mountpoint {}", mtab_entries->size(), root_mountpoint);
    for (auto&& mtab_entry : std::move(*mtab_entries)) {
        if (!mount::umount(mtab_entry.mountpoint)) {
            spdlog::warn("Direct umount failed for {} {}, trying lazy umount", mtab_entry.device, mtab_entry.mountpoint);
            if (auto res = mount::umount(mtab_entry.mountpoint, true); !res) {
                return res;
            }
        }
    }

    for (auto&& zfs_poolname : zfs_poolnames) {
        if (auto res = fs::zpool_export(zfs_poolname); !res) {
            return res;
        }
    }

    return {};
}

}  // namespace gucc::umount
