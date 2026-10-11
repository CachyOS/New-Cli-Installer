#ifndef PARTITION_HPP
#define PARTITION_HPP

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view

namespace gucc::fs {

struct Partition final {
    std::string fstype{};
    std::string mountpoint{};
    std::string uuid_str{};
    std::string device{};

    // partition size,
    // e.g 2G, 512G
    std::string size{};

    // mount points that will be written in fstab,
    // excluding subvol={subvol name}
    // if device is ssd, mount options for ssd should be appended
    std::string mount_opts{};

    /*
    // subvolumes per partition
    // e.g we have partition /dev/nvme0n1p1 with subvolumes: /@, /@home, /@cache
    std::optional<std::vector<BtrfsSubvolume>> subvols;
    */
    // subvolume name if the partition is btrrfs subvolume
    std::optional<std::string> subvolume{};

    /// LUKS data
    std::optional<std::string> luks_mapper_name{};
    std::optional<std::string> luks_uuid{};
    std::optional<std::string> luks_passphrase{};

    [[nodiscard]] constexpr auto is_root() const noexcept -> bool { return mountpoint == "/"; }

    constexpr bool operator==(const Partition&) const = default;
};

[[nodiscard]] constexpr auto is_root_on(const Partition& partition, std::string_view fstype) noexcept -> bool {
    return partition.is_root() && partition.fstype == fstype;
}

}  // namespace gucc::fs

#endif  // PARTITION_HPP
