#include "cachyos/disk.hpp"

#include "gucc/btrfs.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/partition_config.hpp"

#include <algorithm>    // for transform
#include <charconv>     // for from_chars
#include <expected>     // for unexpected
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace {
template <typename T = std::int32_t>
    requires std::numeric_limits<T>::is_integer
inline T to_int(const std::string_view& str) {
    T result = 0;
    std::from_chars(str.data(), str.data() + str.size(), result);
    return result;
}
}  // namespace

namespace cachyos::installer {

auto default_btrfs_subvolumes() noexcept -> std::vector<gucc::fs::BtrfsSubvolume> {
    return {
        gucc::fs::BtrfsSubvolume{.subvolume = "/@"s, .mountpoint = "/"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@home"s, .mountpoint = "/home"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@root"s, .mountpoint = "/root"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@srv"s, .mountpoint = "/srv"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@cache"s, .mountpoint = "/var/cache"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@tmp"s, .mountpoint = "/var/tmp"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@log"s, .mountpoint = "/var/log"s},
        // gucc::fs::BtrfsSubvolume{.subvolume = "/@snapshots"sv, .mountpoint = "/.snapshots"sv},
    };
}

auto get_available_mount_opts(std::string_view fstype) noexcept -> std::vector<std::string> {
    const auto& fs_type = gucc::fs::string_to_filesystem_type(fstype);
    const auto& fs_opts = gucc::fs::get_available_mount_opts(fs_type);

    std::vector<std::string> result{};
    result.reserve(fs_opts.size());
    std::ranges::transform(fs_opts, std::back_inserter(result),
        [](auto&& mount_opt) -> std::string { return mount_opt.name; });
    return result;
}

auto is_volume_removable(std::string_view device) noexcept -> bool {
    // NOTE: for /mnt on /dev/mapper/cryptroot `root_name` will be cryptroot
    const auto& root_name = gucc::utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");

    // NOTE: for /mnt on /dev/mapper/cryptroot on /dev/sda2 with `root_name`=cryptroot, `root_device` will be sda
    const auto& root_device = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e '/{}/,/disk/p' | {}"), root_name, "awk '/disk/ {print $1}'"));
    spdlog::info("root_name: {}. root_device: {}", root_name, root_device);
    const auto& removable = gucc::utils::exec(fmt::format(FMT_COMPILE("cat /sys/block/{}/removable"), root_device));
    return to_int(removable) == 1;
}

auto auto_partition(std::string_view /*device*/, std::string_view /*system_mode*/,
    gucc::bootloader::BootloaderType /*bootloader*/, const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<std::vector<gucc::fs::Partition>, std::string> {
    return std::unexpected("not yet implemented");
}

auto apply_mount_selections(const MountSelections& /*selections*/,
    std::string_view /*mountpoint*/) noexcept
    -> std::expected<std::vector<gucc::fs::Partition>, std::string> {
    return std::unexpected("not yet implemented");
}

auto apply_btrfs_subvolumes(const std::vector<gucc::fs::BtrfsSubvolume>& /*subvols*/,
    const RootPartitionSelection& /*selection*/,
    std::string_view /*mountpoint*/,
    std::vector<gucc::fs::Partition>& /*partitions*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto setup_esp_partition(std::string_view /*device*/, std::string_view /*mountpoint*/,
    std::string_view /*root_mountpoint*/, bool /*format_requested*/) noexcept
    -> std::expected<gucc::fs::Partition, std::string> {
    return std::unexpected("not yet implemented");
}

auto umount_partitions(std::string_view /*mountpoint*/,
    const std::vector<std::string>& /*zfs_pools*/,
    std::string_view /*swap_device*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto generate_fstab(std::string_view /*mountpoint*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

}  // namespace cachyos::installer
