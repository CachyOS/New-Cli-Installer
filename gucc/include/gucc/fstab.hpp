#ifndef FSTAB_HPP
#define FSTAB_HPP

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::fs {

struct Partition final {
    std::string fstype;
    std::string mountpoint;
    std::string uuid_str;
    std::string device;

    // mount points that will be written in fstab,
    // excluding subvol={subvol name}
    // if device is ssd, mount options for ssd should be appended
    std::string mount_opts;
    std::optional<std::string> luks_mapper_name;
    std::optional<std::string> luks_uuid;

    /*
    // subvolumes per partition
    // e.g we have partition /dev/nvme0n1p1 with subvolumes: /@, /@home, /@cache
    std::optional<std::vector<BtrfsSubvolume>> subvols;
    */
    // subvolume name if the partition is btrrfs subvolume
    std::optional<std::string> subvolume;
};

// Generate fstab
auto generate_fstab(const std::vector<Partition>& partitions, std::string_view root_mountpoint, std::string_view crypttab_opts) noexcept -> bool;

// Generate fstab into string
auto generate_fstab_content(const std::vector<Partition>& partitions) noexcept -> std::string;

}  // namespace gucc::fs

#endif  // FSTAB_HPP
