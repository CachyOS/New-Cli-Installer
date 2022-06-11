#ifndef DISK_HPP
#define DISK_HPP

#include <string>
#include <string_view>
#include <vector>

namespace utils {

struct disk_part {
    const std::string_view root{};
    const std::string_view part{};
    const std::string_view mount_opts{};
};

void btrfs_create_subvols(const disk_part& disk, const std::string_view& mode, bool ignore_note = false) noexcept;
void mount_existing_subvols(const disk_part& disk) noexcept;
std::vector<std::string> lvm_show_vg() noexcept;

// ZFS filesystem
void zfs_create_dataset(const std::string_view& zpath, const std::string_view& zmount) noexcept;
std::string zfs_list_devs() noexcept;
std::string zfs_list_datasets(const std::string_view& type = "none") noexcept;

// Other filesystems
void select_filesystem(const std::string_view& fs) noexcept;

}  // namespace utils

#endif  // DISK_HPP
