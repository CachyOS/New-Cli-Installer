#include "gucc/btrfs.hpp"
#include "gucc/io_utils.hpp"

#include <filesystem>

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace {

// same behaviour as os.path.dirname from python
constexpr auto get_dirname(std::string_view full_path) noexcept -> std::string_view {
    if (full_path == "/") {
        return full_path;
    }
    auto pos = full_path.find_last_of('/');
    if (pos == std::string_view::npos) {
        return {};
    }
    return full_path.substr(0, pos);
}

}  // namespace

namespace gucc::fs {

auto btrfs_create_subvol(std::string_view subvolume, std::string_view root_mountpoint) noexcept -> bool {
    const auto& subvol_dirs_path = fmt::format(FMT_COMPILE("{}{}"), root_mountpoint, get_dirname(subvolume));
    std::error_code err{};
    ::fs::create_directories(subvol_dirs_path, err);
    if (err) {
        spdlog::error("Failed to create directories for btrfs subvolume {}: {}", subvol_dirs_path, err.message());
        return false;
    }
    auto cmd = fmt::format(FMT_COMPILE("btrfs subvolume create {}{} 2>>/tmp/cachyos-install.log"), root_mountpoint, subvolume);
    return utils::exec(cmd, true) == "0";
}

auto btrfs_create_subvols(const std::vector<BtrfsSubvolume>& subvols, std::string_view device, std::string_view root_mountpoint, std::string_view mount_opts) noexcept -> bool {
    // Create subvolumes
    for (const auto& subvol : subvols) {
        if (subvol.subvolume.empty()) {
            continue;
        }
        if (!fs::btrfs_create_subvol(subvol.subvolume, root_mountpoint)) {
            spdlog::error("Failed to create btrfs subvolume {} on root mountpoint {}", subvol.subvolume, root_mountpoint);
            return false;
        }
    }
    // TODO(vnepogodin): handle exit code
    utils::exec(fmt::format(FMT_COMPILE("umount -v {} &>>/tmp/cachyos-install.log"), root_mountpoint));

    // Mount subvolumes
    for (const auto& subvol : subvols) {
        auto mount_option = fmt::format(FMT_COMPILE("subvol={},{}"), subvol.subvolume, mount_opts);
        if (subvol.subvolume.empty()) {
            mount_option = mount_opts;
        }

        const auto& subvolume_mountpoint = fmt::format(FMT_COMPILE("{}{}"), root_mountpoint, subvol.subvolume);

        // TODO(vnepogodin): refactor create dir and mount into own function
        std::error_code err{};
        ::fs::create_directories(subvolume_mountpoint, err);
        if (err) {
            spdlog::error("Failed to create directories for btrfs subvols mountpoint {}: {}", subvolume_mountpoint, err.message());
            return false;
        }
        // TODO(vnepogodin): handle exit code
        utils::exec(fmt::format(FMT_COMPILE("mount -o {} \"{}\" {}"), mount_option, device, subvolume_mountpoint));
    }
    return true;
}

}  // namespace gucc::fs
