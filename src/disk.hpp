#ifndef DISK_HPP
#define DISK_HPP

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace utils {

struct disk_part {
    const std::string_view root{};
    const std::string_view part{};
    const std::string_view mount_opts{};
};

void btrfs_create_subvols(const disk_part& disk, const std::string_view& mode, bool ignore_note = false) noexcept;
void mount_existing_subvols(const disk_part& disk) noexcept;
auto lvm_show_vg() noexcept -> std::vector<std::string>;

// ZFS filesystem
[[nodiscard]] bool zfs_auto_pres(const std::string_view& partition, const std::string_view& zfs_zpool_name) noexcept;
[[nodiscard]] bool zfs_create_zpool(const std::string_view& partition, const std::string_view& pool_name) noexcept;

// Other filesystems
void select_filesystem(const std::string_view& fs) noexcept;

}  // namespace utils

#endif  // DISK_HPP
