#include "gucc/zfs.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>   // for find
#include <filesystem>  // for exists, copy_file, create_directories
#include <ranges>      // for ranges::*

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
namespace fs = std::filesystem;

namespace gucc::fs {

// Creates a zfs volume
auto zfs_create_zvol(std::string_view zsize, std::string_view zpath) noexcept -> Result<void> {
    if (!utils::exec_checked(fmt::format(FMT_COMPILE("zfs create -V {}M {}"), zsize, zpath))) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to create zfs zvol {} of size {}M", zpath, zsize));
    }
    return {};
}

// Creates a zfs filesystem, the first parameter is the ZFS path and the second is the mount path
auto zfs_create_dataset(std::string_view zpath, std::string_view zmount) noexcept -> Result<void> {
    if (!utils::exec_checked(fmt::format(FMT_COMPILE("zfs create -o mountpoint={} {}"), zmount, zpath))) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to create zfs dataset {} at mountpoint {}", zpath, zmount));
    }
    return {};
}

auto zfs_create_datasets(const std::vector<ZfsDataset>& zdatasets) noexcept -> Result<void> {
    // Create datasets
    for (const auto& zdataset : zdatasets) {
        if (auto res = fs::zfs_create_dataset(zdataset.zpath, zdataset.mountpoint); !res) {
            return res;
        }
    }
    return {};
}

auto zfs_destroy_dataset(std::string_view zdataset) noexcept -> Result<void> {
    if (!utils::exec_checked(fmt::format(FMT_COMPILE("zfs destroy -r {}"), zdataset))) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to destroy zfs dataset {}", zdataset));
    }
    return {};
}

// returns a list of imported zpools
auto zfs_list_pools() noexcept -> std::string {
    return utils::exec(R"(zfs list -H -o name 2>/dev/null | grep "/")"sv);
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
    if (type == "zvol"sv) {
        return utils::exec("zfs list -Ht volume -o name,volsize 2>/dev/null"sv);
    } else if (type == "legacy"sv) {
        return utils::exec(R"(zfs list -Ht filesystem -o name,mountpoint 2>/dev/null | grep "^.*/.*legacy$" | awk '{print $1}')"sv);
    }

    return utils::exec(R"(zfs list -H -o name 2>/dev/null | grep "/")"sv);
}

auto zfs_set_property(std::string_view property, std::string_view dataset) noexcept -> Result<void> {
    if (!utils::exec_checked(fmt::format(FMT_COMPILE("zfs set {} {}"), property, dataset))) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to set zfs property {} on {}", property, dataset));
    }
    return {};
}

auto zpool_set_property(std::string_view property, std::string_view pool_name) noexcept -> Result<void> {
    const auto& zfs_zpool_cmd = fmt::format(FMT_COMPILE("zpool set {} {}"), property, pool_name);

    spdlog::debug("setting zfs zpool property with: {}", zfs_zpool_cmd);
    if (!utils::exec_checked(zfs_zpool_cmd)) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to set zfs zpool property with: {}", zfs_zpool_cmd));
    }
    return {};
}

auto zfs_create_zpool(std::string_view device_path, std::string_view pool_name, std::string_view pool_options, std::optional<std::string_view> passphrase) noexcept -> Result<void> {
    // ensure hostid exists before pool creation
    {
        std::error_code ec;
        if (!::fs::exists("/etc/hostid"sv, ec) && !utils::exec_checked("zgenhostid"sv)) {
            spdlog::warn("cannot generate hostid with zgenhostid");
        }
    }

    const auto& zfs_zpool_cmd = [&]() {
        auto cmd = fmt::format(FMT_COMPILE("zpool create {}"), pool_options);
        if (passphrase.has_value()) {
            cmd += " -O encryption=aes-256-gcm -O keyformat=passphrase"sv;
        }
        cmd += fmt::format(FMT_COMPILE(" '{}' '{}'"), pool_name, device_path);

        if (passphrase.has_value()) {
            return fmt::format(FMT_COMPILE("echo '{}' | {}"), *passphrase, cmd);
        }
        return cmd;
    }();

    spdlog::debug("creating zfs zpool with: {}", zfs_zpool_cmd);
    if (!utils::exec_checked(zfs_zpool_cmd)) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to create zfs zpool with: {}", zfs_zpool_cmd));
    }
    return {};
}

auto zfs_create_with_config(std::string_view device_path, const fs::ZfsSetupConfig& zfs_config) noexcept -> Result<void> {
    // first we need to create a zpool to hold the datasets/zvols
    if (auto res = fs::zfs_create_zpool(device_path, zfs_config.zpool_name, zfs_config.zpool_options, zfs_config.passphrase); !res) {
        return res;
    }

    // next create the datasets including their parents
    if (auto res = fs::zfs_create_datasets(zfs_config.datasets); !res) {
        return res;
    }

    // find rootfs in zfs datasets
    auto rootfs = std::ranges::find(zfs_config.datasets, "/"sv, &fs::ZfsDataset::mountpoint);
    if (rootfs != std::ranges::end(zfs_config.datasets)) {
        // set bootfs flag used by zfsbootmenu
        const auto& zpool_property = fmt::format(FMT_COMPILE("bootfs={}"), rootfs->zpath);
        if (auto res = fs::zpool_set_property(zpool_property, zfs_config.zpool_name); !res) {
            return res;
        }
    }

    // export zpool to import it later
    const auto& zfs_export_cmd = fmt::format(FMT_COMPILE("zpool export {}"), zfs_config.zpool_name);
    if (!utils::exec_checked(zfs_export_cmd)) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to export zfs zpool with: {}", zfs_export_cmd));
    }
    return {};
}

auto copy_hostid_to_target(std::string_view target_root, std::string_view host_hostid) noexcept -> Result<void> {
    std::error_code ec;
    if (!::fs::exists(host_hostid, ec)) {
        spdlog::warn("hostid: source '{}' does not exist; skipping copy into target", host_hostid);
        return {};
    }

    const auto etc_dir = fmt::format(FMT_COMPILE("{}/etc"), target_root);
    ::fs::create_directories(etc_dir, ec);
    if (ec) {
        return make_error(ErrorCode::FileIo, fmt::format("hostid: failed to create '{}': {}", etc_dir, ec.message()));
    }

    const auto dest = fmt::format(FMT_COMPILE("{}/hostid"), etc_dir);
    ::fs::copy_file(host_hostid, dest, ::fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return make_error(ErrorCode::FileIo, fmt::format("hostid: failed to copy '{}' -> '{}': {}", host_hostid, dest, ec.message()));
    }

    spdlog::info("hostid: copied '{}' -> '{}'", host_hostid, dest);
    return {};
}

}  // namespace gucc::fs
