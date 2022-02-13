#ifndef DISK_HPP
#define DISK_HPP

#include <string_view>

namespace utils {

struct disk_part {
    const std::string_view root{};
    const std::string_view part{};
    const std::string_view mount_opts{};
};

void btrfs_create_subvols(const disk_part& disk, const std::string_view& mode) noexcept;
void mount_existing_subvols(const disk_part& disk) noexcept;

}  // namespace utils

#endif  // DISK_HPP
