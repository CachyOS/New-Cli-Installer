#include "gucc/install.hpp"
#include "gucc/chwd.hpp"
#include "gucc/error.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/fstab.hpp"
#include "gucc/initcpio.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/locale.hpp"
#include "gucc/subprocess.hpp"
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

auto run_rate_mirrors(gucc::utils::SubProcess& child) noexcept -> bool {
    spdlog::info("Running rate-mirrors...");
    if (!gucc::utils::exec_follow({"/bin/bash", "-c", "pacman -Sy --noconfirm --needed cachyos-rate-mirrors rate-mirrors 2>>/tmp/cachyos-install.log"}, child)) {
        spdlog::error("Failed to install cachyos-rate-mirrors");
        return false;
    }
    if (!gucc::utils::exec_follow({"/bin/bash", "-c", "cachyos-rate-mirrors 2>>/tmp/cachyos-install.log"}, child)) {
        spdlog::error("Failed to run cachyos-rate-mirrors");
        return false;
    }
    return true;
}
}  // namespace

namespace gucc::install {

auto install_base(const InstallConfig& config, utils::SubProcess& child) noexcept -> Result<void> {
    const auto& mountpoint = config.mountpoint;

    // Validate required fields
    if (config.keymap.empty()) {
        return make_error(ErrorCode::InvalidArgument, fmt::format("keymap must not be empty"));
    }
    if (config.mountpoint.empty()) {
        return make_error(ErrorCode::InvalidArgument, fmt::format("mountpoint must not be empty"));
    }

    // 1. Create obligatory directories on target
    {
        std::error_code ec;
        ::fs::create_directories(fmt::format(FMT_COMPILE("{}/etc"), mountpoint), ec);
        if (ec) {
            return make_error(ErrorCode::FileIo, fmt::format("Failed to create /etc directory on target: {}", ec.message()));
        }
    }

    // 2. Set console keymap (needed for mkinitcpio hook)
    spdlog::info("Setting up vconsole");
    if (auto res = gucc::locale::set_keymap(config.keymap, mountpoint); !res) {
        return res;
    }

    // 3. Rate mirrors before install
    if (!run_rate_mirrors(child)) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to update rate-mirrors"));
    }

    // 4. Run pacstrap
    if (!gucc::utils::run_pacstrap(mountpoint, config.packages, config.hostcache, child)) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("pacstrap failed"));
    }

    // 5. Copy host files into target
    for (const auto& [src, dst_relative] : config.host_files_to_copy) {
        const auto dst = fmt::format(FMT_COMPILE("{}{}"), mountpoint, dst_relative);
        std::error_code ec;
        ::fs::copy_file(src, dst, ::fs::copy_options::overwrite_existing, ec);
        if (ec) {
            return make_error(ErrorCode::FileIo, fmt::format("Failed to copy '{}' -> '{}': {}", src, dst, ec.message()));
        }
    }

    // 6. Configure mkinitcpio
    const auto initcpio_path = fmt::format(FMT_COMPILE("{}/etc/mkinitcpio.conf"), mountpoint);
    if (auto res = gucc::initcpio::setup_initcpio_config(initcpio_path, config.initcpio_config); !res) {
        return res;
    }

    // 7. Regenerate initramfs with the new mkinitcpio config
    spdlog::info("Regenerating initramfs...");
    if (!gucc::utils::arch_chroot_follow("mkinitcpio -P"sv, mountpoint, child)) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to regenerate initramfs"));
    }

    // 8. Generate fstab
    if (!gucc::fs::run_genfstab_on_mount(mountpoint)) {
        return make_error(ErrorCode::FileIo, fmt::format("Failed to generate fstab"));
    }

    // 9. Install hardware drivers
    if (auto res = gucc::chwd::install_available_profiles(mountpoint, child); !res) {
        return res;
    }

    // 9. Enable required systemd services for the configuration
    if (config.is_zfs) {
        for (auto&& service_name : {"zfs.target"sv, "zfs-import-cache"sv, "zfs-mount"sv, "zfs-import.target"sv}) {
            if (!gucc::services::enable_systemd_service(service_name, mountpoint)) {
                return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to enable required ZFS service '{}'", service_name));
            }
        }
    }

    // 10. Enable additional services from config
    for (const auto& service_name : config.services_to_enable) {
        if (!gucc::services::enable_systemd_service(service_name, mountpoint)) {
            return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to enable service '{}'", service_name));
        }
    }

    // 11. ZFS-specific: copy zpool cachefile
    if (config.is_zfs) {
        if (!copy_zfs_cachefile(mountpoint)) {
            return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to copy ZFS zpool cachefile"));
        }
    }

    return {};
}

}  // namespace gucc::install
