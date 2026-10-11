#include "gucc/install.hpp"
#include "gucc/chwd.hpp"
#include "gucc/error.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/fstab.hpp"
#include "gucc/initcpio.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/locale.hpp"
#include "gucc/mirrors.hpp"
#include "gucc/repos.hpp"
#include "gucc/systemd_services.hpp"
#include "gucc/zfs.hpp"

#include <filesystem>  // for copy_file, copy_options, create_directories
#include <string>      // for string

#include <fmt/compile.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
namespace fs = std::filesystem;

namespace {

static constexpr auto kHostPacmanConf   = "/etc/pacman.conf"sv;
static constexpr auto kTargetPacmanConf = "/tmp/cachyos-installer-pacman.conf"sv;

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
    if (auto res = gucc::utils::exec_or_error(zpool_cmd, "set zpool cachefile"sv); !res) {
        return res;
    }
    if (auto res = gucc::file_utils::create_directories(fmt::format(FMT_COMPILE("{}/etc/zfs"), mountpoint)); !res) {
        return res;
    }

    static constexpr auto cache_src = "/etc/zfs/zpool.cache"sv;
    return gucc::file_utils::copy_file(cache_src, fmt::format(FMT_COMPILE("{}{}"), mountpoint, cache_src));
}

}  // namespace

namespace gucc::install {

auto install_base(const InstallConfig& config) noexcept -> Result<void> {
    const auto& mountpoint = config.mountpoint;

    // Validate required fields
    if (config.keymap.empty()) {
        return make_error(ErrorCode::InvalidArgument, fmt::format("keymap must not be empty"));
    }
    if (config.mountpoint.empty()) {
        return make_error(ErrorCode::InvalidArgument, fmt::format("mountpoint must not be empty"));
    }

    // 1. Create obligatory directories on target
    if (auto res = file_utils::create_directories(fmt::format(FMT_COMPILE("{}/etc"), mountpoint)); !res) {
        return res;
    }

    // 2. Set console keymap (needed for mkinitcpio hook)
    spdlog::info("Setting up vconsole");
    if (auto res = gucc::locale::set_keymap(config.keymap, mountpoint); !res) {
        return res;
    }

    // 3. Rate mirrors before install
    if (auto res = gucc::mirrors::rank_mirrors(); !res) {
        return res;
    }

    // 4. Create pacman.conf for target pacstrap
    if (auto res = gucc::repos::create_target_pacman_config(kHostPacmanConf, kTargetPacmanConf); !res) {
        return res;
    }
    if (!gucc::utils::run_pacstrap(mountpoint, config.packages, kTargetPacmanConf, config.hostcache)) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("pacstrap failed"));
    }

    // 5. Copy host files into target
    for (const auto& [src, dst_relative] : config.host_files_to_copy) {
        if (auto res = file_utils::copy_file(src, fmt::format(FMT_COMPILE("{}{}"), mountpoint, dst_relative)); !res) {
            return res;
        }
    }

    // For a ZFS root, copy host id to bundle with initramfs
    if (config.is_zfs) {
        if (auto res = gucc::fs::copy_hostid_to_target(mountpoint); !res) {
            return res;
        }
    }

    // 6. Configure mkinitcpio
    const auto initcpio_path = fmt::format(FMT_COMPILE("{}/etc/mkinitcpio.conf"), mountpoint);
    if (auto res = gucc::initcpio::setup_initcpio_config(initcpio_path, config.initcpio_config); !res) {
        return res;
    }

    // 7. Regenerate initramfs with the new mkinitcpio config
    spdlog::info("Regenerating initramfs...");
    if (auto res = gucc::utils::arch_chroot_or_error("mkinitcpio -P"sv, mountpoint, "regenerate initramfs"sv); !res) {
        return res;
    }

    // 8. Generate fstab
    if (auto res = gucc::fs::run_genfstab_on_mount(mountpoint); !res) {
        return res;
    }

    // 9. Install hardware drivers
    if (auto res = gucc::chwd::install_available_profiles(mountpoint); !res) {
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
