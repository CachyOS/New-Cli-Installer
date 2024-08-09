#include "doctest_compatibility.h"

#include "gucc/crypttab.hpp"
#include "gucc/logger.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

static constexpr auto CRYPTTAB_EMPTY_TEST = R"(# Configuration for encrypted block devices
# See crypttab(5) for details.

# NOTE: Do not list your root (/) partition here, it must be set up
#       beforehand by the initramfs (/etc/mkinitcpio.conf).

# <name>       <device>                                     <password>              <options>
)"sv;

static constexpr auto CRYPTTAB_UNENCR_BOOT_TEST = R"(# Configuration for encrypted block devices
# See crypttab(5) for details.

# NOTE: Do not list your root (/) partition here, it must be set up
#       beforehand by the initramfs (/etc/mkinitcpio.conf).

# <name>       <device>                                     <password>              <options>
luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f     none
)"sv;

TEST_CASE("crypttab gen test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    const auto& uuid_str        = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s;
    const auto& btrfs_mountopts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s;
    const auto& xfs_mountopts   = "defaults,lazytime,noatime,attr2,inode64,logbsize=256k,noquota"s;

    SECTION("btrfs with subvolumes")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .uuid_str = uuid_str, .device = "/dev/nvme0n1p1"s, .mount_opts = btrfs_mountopts, .subvolume = "/@"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/home"s, .uuid_str = uuid_str, .device = "/dev/nvme0n1p1"s, .mount_opts = btrfs_mountopts, .subvolume = "/@home"s},
            gucc::fs::Partition{
                .fstype     = "btrfs"s,
                .mountpoint = "/var/cache"s,
                .uuid_str   = uuid_str,
                .device     = "/dev/nvme0n1p1"s,
                .mount_opts = btrfs_mountopts,
                .subvolume  = "/@cache"s,
            },
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s, .luks_mapper_name = "", .luks_uuid = ""},
        };
        const auto& crypttab_content = gucc::fs::generate_crypttab_content(partitions, "luks"sv);
        REQUIRE_EQ(crypttab_content, CRYPTTAB_EMPTY_TEST);
    }
    SECTION("basic xfs")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "xfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = xfs_mountopts, .luks_mapper_name = ""},
            gucc::fs::Partition{.fstype = "fat16"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s, .luks_uuid = ""},
        };
        const auto& crypttab_content = gucc::fs::generate_crypttab_content(partitions, "luks"sv);
        REQUIRE_EQ(crypttab_content, CRYPTTAB_EMPTY_TEST);
    }
    SECTION("luks xfs")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "xfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = xfs_mountopts, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s},
            gucc::fs::Partition{.fstype = "linuxswap"s, .mountpoint = ""s, .uuid_str = ""s, .device = "/dev/nvme0n1p3"s, .mount_opts = "defaults,noatime"s},
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& crypttab_content = gucc::fs::generate_crypttab_content(partitions, "luks"sv);
        REQUIRE_EQ(crypttab_content, CRYPTTAB_UNENCR_BOOT_TEST);
    }
    SECTION("zfs")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& crypttab_content = gucc::fs::generate_crypttab_content(partitions, "luks"sv);
        REQUIRE_EQ(crypttab_content, CRYPTTAB_EMPTY_TEST);
    }
    SECTION("luks btrfs with subvolumes")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .device = "/dev/nvme0n1p1"s, .mount_opts = btrfs_mountopts, .subvolume = "/@"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/home"s, .device = "/dev/nvme0n1p1"s, .mount_opts = btrfs_mountopts, .subvolume = "/@home"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/var/cache"s, .device = "/dev/nvme0n1p1"s, .mount_opts = btrfs_mountopts, .subvolume = "/@cache"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& crypttab_content = gucc::fs::generate_crypttab_content(partitions, "luks"sv);
        REQUIRE_EQ(crypttab_content, CRYPTTAB_UNENCR_BOOT_TEST);
    }
    SECTION("luks btrfs with subvolumes {shuffled}")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/home"s, .device = "/dev/nvme0n1p1"s, .mount_opts = btrfs_mountopts, .subvolume = "/@home"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .device = "/dev/nvme0n1p1"s, .mount_opts = btrfs_mountopts, .subvolume = "/@"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/var/cache"s, .device = "/dev/nvme0n1p1"s, .mount_opts = btrfs_mountopts, .subvolume = "/@cache"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s},
        };
        const auto& crypttab_content = gucc::fs::generate_crypttab_content(partitions, "luks"sv);
        REQUIRE_EQ(crypttab_content, CRYPTTAB_UNENCR_BOOT_TEST);
    }

    /*


{'claimed': False, 'device': '', 'features': {}, 'fs': 'unknown', 'fsName': 'unknown', 'mountPoint': '', 'partattrs': 0, 'partlabel': '', 'parttype': '', 'partuuid': '', 'uuid': ''} None
{'claimed': True, 'device': '/dev/sda1', 'features': {}, 'fs': 'fat32', 'fsName': 'fat32', 'mountPoint': '/boot', 'partattrs': 0, 'partlabel': '', 'parttype': '', 'partuuid': '57F0553F-8C12-46DC-BDA7-868728368EEF', 'uuid': '86F8-2CD4'} None
{'claimed': True, 'device': '/dev/sda2', 'features': {}, 'fs': 'btrfs', 'fsName': 'luks2', 'luksMapperName': 'luks-6363636e-bc94-46fa-8ede-86cb57295161', 'luksPassphrase': '123456789', 'luksUuid': '6363636e-bc94-46fa-8ede-86cb57295161', 'mountPoint': '/', 'partattrs': 0, 'partlabel': 'root', 'parttype': '', 'partuuid': '2B6DA8D5-9A95-4917-B736-8F204E393488', 'uuid': '6363636e-bc94-46fa-8ede-86cb57295161'} {'name': 'luks-6363636e-bc94-46fa-8ede-86cb57295161', 'device': 'UUID=6363636e-bc94-46fa-8ede-86cb57295161', 'password': 'none', 'options': ''}


     */
}
