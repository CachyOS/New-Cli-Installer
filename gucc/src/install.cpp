#include "gucc/install.hpp"
#include "gucc/chwd.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/fstab.hpp"
#include "gucc/initcpio.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/locale.hpp"
#include "gucc/systemd_services.hpp"

#include <filesystem>  // for copy_file, copy_options, create_directories
#include <string>      // for string

#include <fmt/compile.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
namespace fs = std::filesystem;

namespace {
auto copy_zfs_cachefile(std::string_view mountpoint) noexcept -> bool {
    const auto& zfs_source = gucc::fs::utils::get_mountpoint_source(mountpoint);
    if (zfs_source.empty()) {
        spdlog::error("Failed to get ZFS source for mountpoint '{}'", mountpoint);
        return false;
    }

    // Extract pool name
    const auto slash_pos  = zfs_source.find('/');
    const auto& pool_name = (slash_pos != std::string::npos)
        ? zfs_source.substr(0, slash_pos)
        : zfs_source;

    const auto& zpool_cmd = fmt::format(FMT_COMPILE("zpool set cachefile=/etc/zfs/zpool.cache {}"), pool_name);
    if (!gucc::utils::exec_checked(zpool_cmd)) {
        spdlog::error("Failed to set zpool cachefile for pool '{}'", pool_name);
        return false;
    }

    std::error_code ec;
    ::fs::create_directories(fmt::format(FMT_COMPILE("{}/etc/zfs"), mountpoint), ec);
    if (ec) {
        spdlog::error("Failed to create /etc/zfs directory: {}", ec.message());
        return false;
    }

    const auto cache_src = "/etc/zfs/zpool.cache"sv;
    const auto cache_dst = fmt::format(FMT_COMPILE("{}{}"), mountpoint, cache_src);
    ::fs::copy_file(cache_src, cache_dst, ::fs::copy_options::overwrite_existing, ec);
    if (ec) {
        spdlog::error("Failed to copy zpool.cache: {}", ec.message());
        return false;
    }
    return true;
}
}  // namespace

namespace gucc::install {

auto install_base(const InstallConfig& config, utils::SubProcess& child) noexcept -> bool {
    const auto& mountpoint = config.mountpoint;

    // Validate required fields
    if (config.keymap.empty()) {
        spdlog::error("keymap must not be empty");
        return false;
    }
    if (config.mountpoint.empty()) {
        spdlog::error("mountpoint must not be empty");
        return false;
    }

    // 1. Create obligatory directories on target
    {
        std::error_code ec;
        ::fs::create_directories(fmt::format(FMT_COMPILE("{}/etc"), mountpoint), ec);
        if (ec) {
            spdlog::error("Failed to create /etc directory on target: {}", ec.message());
            return false;
        }
    }

    // 2. Set console keymap (needed for mkinitcpio hook)
    spdlog::info("Setting up vconsole");
    if (!gucc::locale::set_keymap(config.keymap, mountpoint)) {
        spdlog::error("Failed to set keymap '{}'", config.keymap);
        return false;
    }

    // 3. Run pacstrap
    if (!gucc::utils::run_pacstrap(mountpoint, config.packages, config.hostcache, child)) {
        spdlog::error("pacstrap failed");
        return false;
    }

    // 4. Copy host files into target
    for (const auto& [src, dst_relative] : config.host_files_to_copy) {
        const auto dst = fmt::format(FMT_COMPILE("{}{}"), mountpoint, dst_relative);
        std::error_code ec;
        ::fs::copy_file(src, dst, ::fs::copy_options::overwrite_existing, ec);
        if (ec) {
            spdlog::error("Failed to copy '{}' -> '{}': {}", src, dst, ec.message());
            return false;
        }
    }

    // 5. Configure mkinitcpio
    const auto initcpio_path = fmt::format(FMT_COMPILE("{}/etc/mkinitcpio.conf"), mountpoint);
    if (!gucc::initcpio::setup_initcpio_config(initcpio_path, config.initcpio_config)) {
        spdlog::error("Failed to setup initcpio config");
        return false;
    }

    // 6. Regenerate initramfs with the new mkinitcpio config
    spdlog::info("Regenerating initramfs...");
    if (!gucc::utils::arch_chroot_follow("mkinitcpio -P"sv, mountpoint, child)) {
        spdlog::error("Failed to regenerate initramfs");
        return false;
    }

    // 7. Generate fstab
    if (!gucc::fs::run_genfstab_on_mount(mountpoint)) {
        spdlog::error("Failed to generate fstab");
        return false;
    }

    // 8. Install hardware drivers
    if (!gucc::chwd::install_available_profiles(mountpoint)) {
        spdlog::error("Failed to install chwd drivers");
        return false;
    }

    // 9. Enable required systemd services for the configuration
    if (config.is_zfs) {
        for (auto&& service_name : {"zfs.target"sv, "zfs-import-cache"sv, "zfs-mount"sv, "zfs-import.target"sv}) {
            if (!gucc::services::enable_systemd_service(service_name, mountpoint)) {
                spdlog::error("Failed to enable required ZFS service '{}'", service_name);
                return false;
            }
        }
    }

    // 10. Enable additional services from config
    for (const auto& service_name : config.services_to_enable) {
        if (!gucc::services::enable_systemd_service(service_name, mountpoint)) {
            spdlog::error("Failed to enable service '{}'", service_name);
            return false;
        }
    }

    // 11. ZFS-specific: copy zpool cachefile
    if (config.is_zfs) {
        return copy_zfs_cachefile(mountpoint);
    }

    return true;
}

}  // namespace gucc::install
