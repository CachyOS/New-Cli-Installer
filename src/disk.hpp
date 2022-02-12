#ifndef DISK_HPP
#define DISK_HPP

#include <string_view>

namespace utils {

struct disk_part {
    const std::string_view root;
    const std::string_view part;
};

void mount_existing_subvols(const disk_part& partition) noexcept;
}  // namespace utils

#endif  // DISK_HPP
