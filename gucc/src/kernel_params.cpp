#include "gucc/kernel_params.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <filesystem>  // for fs::path
#include <optional>    // for optional
#include <utility>     // for make_optional

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

auto is_root_on_btrfs(const gucc::fs::Partition& partition) noexcept -> bool {
    return partition.mountpoint == "/"sv && partition.fstype == "btrfs"sv;
}
auto is_root_on_zfs(const gucc::fs::Partition& partition) noexcept -> bool {
    return partition.mountpoint == "/"sv && partition.fstype == "zfs"sv;
}

}  // namespace

namespace gucc::fs {

// base actions taken from calamares
// see https://github.com/calamares/calamares/blob/calamares/src/modules/bootloader/main.py#L133
auto get_kernel_params(const std::vector<Partition>& partitions, std::string_view kernel_params, std::optional<std::string> zfs_root_dataset) noexcept -> std::optional<std::vector<std::string>> {
    auto kernel_params_list = gucc::utils::make_multiline(kernel_params, false, ' ');
    kernel_params_list.emplace_back("rw"s);

    std::vector<std::string> cryptdevice_params{};
    std::optional<std::string_view> root_uuid{};
    // swap settings
    std::optional<std::string_view> swap_uuid{};
    std::optional<std::string_view> swap_mappername{};

    for (auto&& partition : partitions) {
        const bool has_luks = partition.luks_mapper_name.has_value();
        if (partition.fstype == "linuxswap"sv && !has_luks) {
            swap_uuid = std::make_optional<std::string_view>(partition.uuid_str);
        } else if (partition.fstype == "linuxswap"sv && has_luks) {
            swap_mappername = partition.luks_mapper_name;
        }

        if (partition.mountpoint == "/"sv && has_luks) {
            cryptdevice_params = {fmt::format(FMT_COMPILE("cryptdevice=UUID={}:{}"), *partition.luks_uuid, *partition.luks_mapper_name)};
            cryptdevice_params.push_back(fmt::format(FMT_COMPILE("root=/dev/mapper/{}"), *partition.luks_mapper_name));
        }

        if (partition.mountpoint == "/"sv) {
            root_uuid = std::make_optional<std::string_view>(partition.uuid_str);
        }

        // specific logic for btrfs and zfs

        // If a btrfs root subvolume was not set, it implies that the root
        // is directly on the partition and this option is not required.
        if (is_root_on_btrfs(partition) && partition.subvolume) {
            kernel_params_list.push_back(fmt::format(FMT_COMPILE("rootflags=subvol={}"), *partition.subvolume));
        }
        // The root dataset's location must be specified for ZFS.
        else if (is_root_on_zfs(partition)) {
            if (!zfs_root_dataset) {
                spdlog::error("kernel_params: root zfs dataset cannot be nullopt");
                return std::nullopt;
            }
            kernel_params_list.push_back(fmt::format(FMT_COMPILE("root=ZFS={}"), *zfs_root_dataset));
        }
    }

    if (!root_uuid.has_value() || root_uuid->empty()) {
        spdlog::error("kernel_params: not found ROOT partition UUID");
        return std::nullopt;
    }

    if (!cryptdevice_params.empty()) {
        kernel_params_list.insert(kernel_params_list.end(), cryptdevice_params.begin(), cryptdevice_params.end());
    } else {
        kernel_params_list.push_back(fmt::format(FMT_COMPILE("root=UUID={}"), *root_uuid));
    }

    if (swap_uuid.has_value() && !swap_uuid->empty()) {
        kernel_params_list.push_back(fmt::format(FMT_COMPILE("resume=UUID={}"), *swap_uuid));
    }

    if (swap_mappername.has_value() && !swap_mappername->empty()) {
        kernel_params_list.push_back(fmt::format(FMT_COMPILE("resume=/dev/mapper/{}"), *swap_mappername));
    }

    return std::make_optional<std::vector<std::string>>(kernel_params_list);
}

}  // namespace gucc::fs
