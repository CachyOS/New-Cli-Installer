#include "gucc/zfs.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>  // for find
#include <ranges>     // for ranges::*

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::fs {

// Creates a zfs volume
auto zfs_create_zvol(std::string_view zsize, std::string_view zpath) noexcept -> bool {
#ifdef NDEVENV
    return utils::exec_checked(fmt::format(FMT_COMPILE("zfs create -V {}M {} 2>>/tmp/cachyos-install.log"), zsize, zpath));
#else
    spdlog::debug("zfs create -V {}M {}", zsize, zpath);
    return true;
#endif
}

// Creates a zfs filesystem, the first parameter is the ZFS path and the second is the mount path
auto zfs_create_dataset(std::string_view zpath, std::string_view zmount) noexcept -> bool {
#ifdef NDEVENV
    return utils::exec_checked(fmt::format(FMT_COMPILE("zfs create -o mountpoint={} {} 2>>/tmp/cachyos-install.log"), zmount, zpath));
#else
    spdlog::debug("zfs create -o mountpoint={} {}", zmount, zpath);
    return true;
#endif
}

auto zfs_create_datasets(const std::vector<ZfsDataset>& zdatasets) noexcept -> bool {
    // Create datasets
    for (const auto& zdataset : zdatasets) {
        if (!fs::zfs_create_dataset(zdataset.zpath, zdataset.mountpoint)) {
            spdlog::error("Failed to create zfs dataset {} at mountpoint {}", zdataset.zpath, zdataset.mountpoint);
            return false;
        }
    }
    return true;
}

auto zfs_destroy_dataset(std::string_view zdataset) noexcept -> bool {
#ifdef NDEVENV
    return utils::exec_checked(fmt::format(FMT_COMPILE("zfs destroy -r {} 2>>/tmp/cachyos-install.log"), zdataset));
#else
    spdlog::debug("zfs destroy -r {}", zdataset);
    return true;
#endif
}

// returns a list of imported zpools
auto zfs_list_pools() noexcept -> std::string {
#ifdef NDEVENV
    return utils::exec(R"(zfs list -H -o name 2>/dev/null | grep "/")"sv);
#else
    return {"vol0\nvol1\n"};
#endif
}

// returns a list of devices containing zfs members
auto zfs_list_devs() noexcept -> std::string {
    std::string list_of_devices{};
    // get a list of devices with zpools on them
    auto devices = utils::make_multiline(utils::exec(R"(zpool status -PL 2>/dev/null | awk '{print $1}' | grep "^/")"sv));
    for (auto&& device : std::move(devices)) {
        // add the device
        list_of_devices += fmt::format(FMT_COMPILE("{}\n"), device);
        // now let's add any other forms of those devices
        list_of_devices += utils::exec(fmt::format(FMT_COMPILE("find -L /dev/ -xtype l -samefile {} 2>/dev/null"), device));
    }
    return list_of_devices;
}

auto zfs_list_datasets(std::string_view type) noexcept -> std::string {
#ifdef NDEVENV
    if (type == "zvol"sv) {
        return utils::exec("zfs list -Ht volume -o name,volsize 2>/dev/null"sv);
    } else if (type == "legacy"sv) {
        return utils::exec(R"(zfs list -Ht filesystem -o name,mountpoint 2>/dev/null | grep "^.*/.*legacy$" | awk '{print $1}')"sv);
    }

    return utils::exec(R"(zfs list -H -o name 2>/dev/null | grep "/")"sv);
#else
    spdlog::debug("type := {}", type);
    return {"zpcachyos"};
#endif
}

auto zfs_set_property(std::string_view property, std::string_view dataset) noexcept -> bool {
#ifdef NDEVENV
    return utils::exec_checked(fmt::format(FMT_COMPILE("zfs set {} {} 2>>/tmp/cachyos-install.log"), property, dataset));
#else
    spdlog::debug("zfs set {} {}", property, dataset);
    return true;
#endif
}

auto zpool_set_property(std::string_view property, std::string_view pool_name) noexcept -> bool {
    const auto& zfs_zpool_cmd = fmt::format(FMT_COMPILE("zpool set {} {} 2>>/tmp/cachyos-install.log"), property, pool_name);

    spdlog::debug("setting zfs zpool property with: {}", zfs_zpool_cmd);
    if (!utils::exec_checked(zfs_zpool_cmd)) {
        spdlog::error("Failed to set zfs zpool property with: {}", zfs_zpool_cmd);
        return false;
    }
    return true;
}

auto zfs_create_zpool(std::string_view device_path, std::string_view pool_name, std::string_view pool_options, std::optional<std::string_view> passphrase) noexcept -> bool {
    const auto& zfs_zpool_cmd = [&]() {
        auto cmd = fmt::format(FMT_COMPILE("zpool create {}"), pool_options);
        if (passphrase.has_value()) {
            cmd += "-O encryption=aes-256-gcm -O keyformat=passphrase"sv;
        }
        cmd += fmt::format(FMT_COMPILE("'{}' '{}' 2>>/tmp/cachyos-install.log"), pool_name, device_path);

        if (passphrase.has_value()) {
            return fmt::format(FMT_COMPILE("echo '{}' | {}"), *passphrase, cmd);
        }
        return cmd;
    }();

    spdlog::debug("creating zfs zpool with: {}", zfs_zpool_cmd);
    if (!utils::exec_checked(zfs_zpool_cmd)) {
        spdlog::error("Failed to create zfs zpool with: {}", zfs_zpool_cmd);
        return false;
    }
    return true;
}

auto zfs_create_with_config(std::string_view device_path, const fs::ZfsSetupConfig& zfs_config) noexcept -> bool {
    // first we need to create a zpool to hold the datasets/zvols
    if (!fs::zfs_create_zpool(device_path, zfs_config.zpool_name, zfs_config.zpool_options, zfs_config.passphrase)) {
        spdlog::error("Failed to create zfs pool");
        return false;
    }

    // next create the datasets including their parents
    if (!fs::zfs_create_datasets(zfs_config.datasets)) {
        spdlog::error("Failed to create zfs datasets");
        return false;
    }

    // find rootfs in zfs datasets
    auto rootfs = std::ranges::find(zfs_config.datasets, "/", &fs::ZfsDataset::mountpoint);
    if (rootfs != std::ranges::end(zfs_config.datasets)) {
        // set bootfs flag used by zfsbootmenu
        const auto& zpool_property = fmt::format(FMT_COMPILE("bootfs={}"), rootfs->zpath);
        if (!fs::zpool_set_property(zpool_property, zfs_config.zpool_name)) {
            spdlog::error("Failed to set zfs pool property");
            return false;
        }
    }

    // export zpool to import it later
    const auto& zfs_export_cmd = fmt::format(FMT_COMPILE("zpool export {} 2>>/tmp/cachyos-install.log"), zfs_config.zpool_name);
    if (!utils::exec_checked(zfs_export_cmd)) {
        spdlog::error("Failed to export zfs zpool with: {}", zfs_export_cmd);
        return false;
    }
    return true;
}

}  // namespace gucc::fs
