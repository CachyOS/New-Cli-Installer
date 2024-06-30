#include "gucc/fstab.hpp"

#include <cassert>

#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

static constexpr auto FSTAB_BTRFS_TEST = R"(# Static information about the filesystems.
# See fstab(5) for details.

# <file system> <dir> <type> <options> <dump> <pass>
# /dev/nvme0n1p1
UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f /              btrfs   subvol=/@,defaults,noatime,compress=zstd,space_cache=v2,commit=120 0 0

# /dev/nvme0n1p1
UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f /home          btrfs   subvol=/@home,defaults,noatime,compress=zstd,space_cache=v2,commit=120 0 0

# /dev/nvme0n1p1
UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f /var/cache     btrfs   subvol=/@cache,defaults,noatime,compress=zstd,space_cache=v2,commit=120 0 0

# /dev/nvme0n1p2
UUID=8EFB-4B84                            /boot          vfat    defaults,noatime 0 2

)"sv;

static constexpr auto FSTAB_XFS_TEST = R"(# Static information about the filesystems.
# See fstab(5) for details.

# <file system> <dir> <type> <options> <dump> <pass>
# /dev/nvme0n1p1
UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f /              xfs     defaults,lazytime,noatime,attr2,inode64,logbsize=256k,noquota 0 1

# /dev/nvme0n1p2
UUID=8EFB-4B84                            /boot          vfat    defaults,noatime 0 2

)"sv;

static constexpr auto FSTAB_LUKS_XFS_TEST = R"(# Static information about the filesystems.
# See fstab(5) for details.

# <file system> <dir> <type> <options> <dump> <pass>
# /dev/nvme0n1p1
/dev/mapper/luks_device                   /              xfs     defaults,lazytime,noatime,attr2,inode64,logbsize=256k,noquota 0 1

# /dev/nvme0n1p2
UUID=8EFB-4B84                            /boot          vfat    defaults,noatime 0 2

)"sv;

static constexpr auto FSTAB_ZFS_TEST = R"(# Static information about the filesystems.
# See fstab(5) for details.

# <file system> <dir> <type> <options> <dump> <pass>
# /dev/nvme0n1p2
UUID=8EFB-4B84                            /boot          vfat    defaults,noatime 0 2

)"sv;

static constexpr auto FSTAB_LUKS_BTRFS_TEST = R"(# Static information about the filesystems.
# See fstab(5) for details.

# <file system> <dir> <type> <options> <dump> <pass>
# /dev/nvme0n1p1
/dev/mapper/luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f /              btrfs   subvol=/@,defaults,noatime,compress=zstd,space_cache=v2,commit=120 0 0

# /dev/nvme0n1p1
/dev/mapper/luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f /home          btrfs   subvol=/@home,defaults,noatime,compress=zstd,space_cache=v2,commit=120 0 0

# /dev/nvme0n1p1
/dev/mapper/luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f /var/cache     btrfs   subvol=/@cache,defaults,noatime,compress=zstd,space_cache=v2,commit=120 0 0

# /dev/nvme0n1p2
UUID=8EFB-4B84                            /boot          vfat    defaults,noatime 0 2

)"sv;

int main() {
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg& msg) {
        // noop
    });
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("default", callback_sink));

    // btrfs with subvolumes
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .luks_mapper_name = {}, .subvolume = "/@"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .luks_mapper_name = {}, .subvolume = "/@home"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .luks_mapper_name = {}, .subvolume = "/@cache"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& fstab_content = gucc::fs::generate_fstab_content(partitions);
        assert(fstab_content == FSTAB_BTRFS_TEST);
    }
    // basic xfs
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "xfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,lazytime,noatime,attr2,inode64,logbsize=256k,noquota"s},
            gucc::fs::Partition{.fstype = "fat16"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& fstab_content = gucc::fs::generate_fstab_content(partitions);
        assert(fstab_content == FSTAB_XFS_TEST);
    }
    // luks xfs
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "xfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,lazytime,noatime,attr2,inode64,logbsize=256k,noquota"s, .luks_mapper_name = "luks_device"s, .luks_uuid = "00e1b836-81b6-433f-83ca-0fd373e3cd50"s},
            gucc::fs::Partition{.fstype = "linuxswap"s, .mountpoint = ""s, .uuid_str = ""s, .device = "/dev/nvme0n1p3"s, .mount_opts = "defaults,noatime"s},
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& fstab_content = gucc::fs::generate_fstab_content(partitions);
        assert(fstab_content == FSTAB_LUKS_XFS_TEST);
    }
    // zfs
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& fstab_content = gucc::fs::generate_fstab_content(partitions);
        assert(fstab_content == FSTAB_ZFS_TEST);
    }
    // luks btrfs with subvolumes
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .subvolume = "/@"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/home"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .subvolume = "/@home"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/var/cache"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .subvolume = "/@cache"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& fstab_content = gucc::fs::generate_fstab_content(partitions);
        assert(fstab_content == FSTAB_LUKS_BTRFS_TEST);
    }
}
