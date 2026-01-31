#ifndef PARTITION_HPP
#define PARTITION_HPP

#include <cstdint>   // for int32_t
#include <optional>  // for optional
#include <string>    // for string

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

    /// LUKS version (1 or 2)
    std::optional<std::int32_t> luks_version{};

    /// TPM2 data
    std::optional<bool> tpm2_enrolled{};
    std::optional<std::string> tpm2_pcrs{};  // e.g., "0,2,4,7"

    constexpr bool operator==(const Partition&) const = default;
};

}  // namespace gucc::fs

#endif  // PARTITION_HPP
