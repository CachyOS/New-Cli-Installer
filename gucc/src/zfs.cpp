#include "gucc/zfs.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/ranges_utils.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/zfs_query.hpp"

#include <algorithm>    // for sort
#include <array>        // for array
#include <filesystem>   // for exists, equivalent
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;
namespace fs = std::filesystem;

namespace gucc::fs {

// Creates a zfs volume
auto zfs_create_zvol(std::string_view zsize, std::string_view zpath) noexcept -> Result<void> {
    return utils::exec_or_error({"/usr/bin/zfs"s, "create"s, "-V"s, fmt::format(FMT_COMPILE("{}M"), zsize), std::string{zpath}}, "create zfs zvol"sv);
}

// Creates a zfs filesystem, the first parameter is the ZFS path and the second is the mount path
auto zfs_create_dataset(std::string_view zpath, std::string_view zmount) noexcept -> Result<void> {
    return utils::exec_or_error({"/usr/bin/zfs"s, "create"s, "-o"s, fmt::format(FMT_COMPILE("mountpoint={}"), zmount), std::string{zpath}}, "create zfs dataset"sv);
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
    return utils::exec_or_error({"/usr/bin/zfs"s, "destroy"s, "-r"s, std::string{zdataset}}, "destroy zfs dataset"sv);
}

// returns a list of devices containing zfs members
auto zfs_list_devs() noexcept -> std::string {
    std::string list_of_devices{};
    // get a list of devices with zpools on them
    const auto devices = zfs_query::detail::parse_zpool_status_devices(utils::query({"/usr/bin/zpool"s, "status"s, "-PL"s}));
    for (const auto& device : devices) {
        list_of_devices += fmt::format(FMT_COMPILE("{}\n"), device);
        for (const auto& link : detail::find_symlinks_to(device, "/dev"sv)) {
            list_of_devices += fmt::format(FMT_COMPILE("{}\n"), link);
        }
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
    return utils::exec_or_error({"/usr/bin/zfs"s, "set"s, std::string{property}, std::string{dataset}}, "set zfs property"sv);
}

auto zpool_set_property(std::string_view property, std::string_view pool_name) noexcept -> Result<void> {
    return utils::exec_or_error({"/usr/bin/zpool"s, "set"s, std::string{property}, std::string{pool_name}}, "set zpool property"sv);
}

auto zfs_create_zpool(std::string_view device_path, std::string_view pool_name, std::string_view pool_options, std::optional<std::string_view> passphrase) noexcept -> Result<void> {
    // ensure hostid exists before pool creation
    std::error_code ec;
    if (!::fs::exists("/etc/hostid"sv, ec) && !utils::exec_checked(std::vector{"/usr/bin/zgenhostid"s})) {
        spdlog::warn("cannot generate hostid with zgenhostid");
    }

    std::vector<std::string> argv{"/usr/bin/zpool"s, "create"s};
    argv.append_range(utils::words(pool_options) | utils::to_owned);
    if (passphrase.has_value()) {
        argv.append_range(std::array{"-O"s, "encryption=aes-256-gcm"s, "-O"s, "keyformat=passphrase"s});
    }
    argv.append_range(std::array{std::string{pool_name}, std::string{device_path}});

    // keyformat=passphrase reads a non-tty passphrase line from stdin
    const auto input = passphrase ? fmt::format(FMT_COMPILE("{}\n"), *passphrase) : std::string{};
    return utils::exec_or_error_with_input(argv, input, "create zpool"sv);
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
    return utils::exec_or_error_with_input({"/usr/bin/zfs"s, "load-key"s, std::string{dataset}}, fmt::format(FMT_COMPILE("{}\n"), passphrase), "load zfs encryption key"sv);
}

auto copy_hostid_to_target(std::string_view target_root, std::string_view host_hostid) noexcept -> Result<void> {
    std::error_code ec;
    if (!::fs::exists(host_hostid, ec)) {
        spdlog::warn("hostid: source '{}' does not exist; skipping copy into target", host_hostid);
        return {};
    }

    const auto etc_dir = utils::target_path(target_root, "/etc"sv);
    if (auto res = file_utils::create_directories(etc_dir); !res) {
        return res;
    }
    const auto dest = utils::target_path(etc_dir, "/hostid"sv);
    if (auto res = file_utils::copy_file(host_hostid, dest); !res) {
        return res;
    }

    spdlog::info("hostid: copied '{}' -> '{}'", host_hostid, dest);
    return {};
}

}  // namespace gucc::fs

namespace gucc::fs::detail {

auto find_symlinks_to(std::string_view device, std::string_view search_root) noexcept -> std::vector<std::string> {
    std::error_code ec;
    // symlinked dirs are not followed
    auto it = ::fs::recursive_directory_iterator(search_root, ::fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        return {};
    }

    std::vector<std::string> links;
    for (const auto end = ::fs::recursive_directory_iterator{}; it != end; it.increment(ec)) {
        // ugly shit, but not throwing
        if (ec) {
            break;
        }
        std::error_code entry_ec;
        if (it->is_symlink(entry_ec) && ::fs::equivalent(it->path(), device, entry_ec)) {
            links.emplace_back(it->path().string());
        }
    }
    std::ranges::sort(links);
    return links;
}

}  // namespace gucc::fs::detail
