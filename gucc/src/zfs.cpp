#include "gucc/zfs.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/ranges_utils.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/zfs_query.hpp"

#include <filesystem>  // for exists
#include <ranges>      // for ranges::*
#include <string>      // for string
#include <vector>      // for vector

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;
namespace fs = std::filesystem;

namespace gucc::fs {

// Creates a zfs volume
auto zfs_create_zvol(std::string_view zsize, std::string_view zpath) noexcept -> Result<void> {
    return utils::exec_or_error(fmt::format(FMT_COMPILE("zfs create -V {}M {}"), zsize, zpath), "create zfs zvol"sv);
}

// Creates a zfs filesystem, the first parameter is the ZFS path and the second is the mount path
auto zfs_create_dataset(std::string_view zpath, std::string_view zmount) noexcept -> Result<void> {
    return utils::exec_or_error(fmt::format(FMT_COMPILE("zfs create -o mountpoint={} {}"), zmount, zpath), "create zfs dataset"sv);
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
    return utils::exec_or_error(fmt::format(FMT_COMPILE("zfs destroy -r {}"), zdataset), "destroy zfs dataset"sv);
}

// returns a list of devices containing zfs members
auto zfs_list_devs() noexcept -> std::string {
    std::string list_of_devices{};
    // get a list of devices with zpools on them
    auto devices = zfs_query::detail::parse_zpool_status_devices(utils::exec("zpool status -PL 2>/dev/null"sv));
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
        return utils::join(zfs_query::detail::child_dataset_names(list_zfs_datasets(), true));
    }

    return utils::join(zfs_query::detail::child_dataset_names(list_zfs_datasets()));
}

auto zfs_set_property(std::string_view property, std::string_view dataset) noexcept -> Result<void> {
    return utils::exec_or_error(fmt::format(FMT_COMPILE("zfs set {} {}"), property, dataset), "set zfs property"sv);
}

auto zpool_set_property(std::string_view property, std::string_view pool_name) noexcept -> Result<void> {
    const auto& zfs_zpool_cmd = fmt::format(FMT_COMPILE("zpool set {} {}"), property, pool_name);

    spdlog::debug("setting zfs zpool property with: {}", zfs_zpool_cmd);
    return utils::exec_or_error(zfs_zpool_cmd, "set zpool property"sv);
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
        return cmd;
    }();

    // keyformat=passphrase reads a non-tty passphrase line from stdin
    const auto input = passphrase ? fmt::format(FMT_COMPILE("{}\n"), *passphrase) : std::string{};
    return utils::exec_or_error_with_input(zfs_zpool_cmd, input, "create zpool"sv);
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
    if (const auto* rootfs = utils::find_ptr(zfs_config.datasets, "/"sv, &fs::ZfsDataset::mountpoint)) {
        // set bootfs flag used by zfsbootmenu
        const auto& zpool_property = fmt::format(FMT_COMPILE("bootfs={}"), rootfs->zpath);
        if (auto res = fs::zpool_set_property(zpool_property, zfs_config.zpool_name); !res) {
            return res;
        }
    }

    // export zpool to import it later
    return zpool_export(zfs_config.zpool_name);
}

auto zpool_list_importable() noexcept -> std::vector<std::string> {
    return zfs_query::detail::parse_importable_zpools(utils::exec("zpool import 2>/dev/null"sv));
}

auto zpool_import(std::string_view pool_name, std::string_view altroot, bool mount_datasets) noexcept -> Result<void> {
    std::vector<std::string> argv{"/usr/bin/zpool"s, "import"s};
    if (!mount_datasets) {
        argv.emplace_back("-N"s);
    }
    argv.insert(argv.end(), {"-R"s, std::string{altroot}, std::string{pool_name}});
    return utils::exec_or_error(argv, "import zpool"sv);
}

auto zpool_export(std::string_view pool_name) noexcept -> Result<void> {
    return utils::exec_or_error({"/usr/bin/zpool"s, "export"s, std::string{pool_name}}, "export zpool"sv);
}

auto zfs_mount_all() noexcept -> Result<void> {
    return utils::exec_or_error({"/usr/bin/zfs"s, "mount"s, "-aO"s}, "mount zfs datasets"sv);
}

auto zfs_mount(std::string_view dataset) noexcept -> Result<void> {
    return utils::exec_or_error({"/usr/bin/zfs"s, "mount"s, std::string{dataset}}, "mount zfs dataset"sv);
}

auto zfs_load_key(std::string_view dataset, std::string_view passphrase) noexcept -> Result<void> {
    // keyformat=passphrase reads one line from stdin
    return utils::exec_or_error_with_input(fmt::format(FMT_COMPILE("zfs load-key {}"), dataset), fmt::format(FMT_COMPILE("{}\n"), passphrase), "load zfs encryption key"sv);
}

auto copy_hostid_to_target(std::string_view target_root, std::string_view host_hostid) noexcept -> Result<void> {
    std::error_code ec;
    if (!::fs::exists(host_hostid, ec)) {
        spdlog::warn("hostid: source '{}' does not exist; skipping copy into target", host_hostid);
        return {};
    }

    const auto etc_dir = fmt::format(FMT_COMPILE("{}/etc"), target_root);
    if (auto res = file_utils::create_directories(etc_dir); !res) {
        return res;
    }
    const auto dest = fmt::format(FMT_COMPILE("{}/hostid"), etc_dir);
    if (auto res = file_utils::copy_file(host_hostid, dest); !res) {
        return res;
    }

    spdlog::info("hostid: copied '{}' -> '{}'", host_hostid, dest);
    return {};
}

}  // namespace gucc::fs
