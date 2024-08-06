#include "doctest_compatibility.h"

#include "gucc/btrfs.hpp"
#include "gucc/logger.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST_CASE("BTRFS test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    const std::vector<gucc::fs::BtrfsSubvolume> subvolumes{
        gucc::fs::BtrfsSubvolume{.subvolume = "/@"s, .mountpoint = "/"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@home"s, .mountpoint = "/home"s},
        gucc::fs::BtrfsSubvolume{.subvolume = "/@cache"s, .mountpoint = "/var/cache"s},
        // gucc::fs::BtrfsSubvolume{.subvolume = "/@snapshots"sv, .mountpoint = "/.snapshots"sv},
    };

    SECTION("btrfs with subvolumes")
    {
        const std::vector<gucc::fs::Partition> expected_partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@home"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@cache"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        REQUIRE(gucc::fs::btrfs_append_subvolumes(partitions, subvolumes));
        REQUIRE(partitions.size() == 4);
        REQUIRE(partitions == expected_partitions);
    }
    SECTION("invalid btrfs with subvolumes")
    {
        const std::vector<gucc::fs::Partition> expected_partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@home"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@cache"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@home"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@cache"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        REQUIRE(!gucc::fs::btrfs_append_subvolumes(partitions, subvolumes));
        REQUIRE(partitions.size() == 3);
        REQUIRE(partitions == expected_partitions);
    }
    SECTION("invalid (without root part) btrfs with subvolumes")
    {
        const std::vector<gucc::fs::Partition> expected_partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@home"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@cache"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@home"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@cache"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        REQUIRE(!gucc::fs::btrfs_append_subvolumes(partitions, subvolumes));
        REQUIRE(partitions.size() == 3);
        REQUIRE(partitions == expected_partitions);
    }
    SECTION("btrfs without subvolumes")
    {
        const std::vector<gucc::fs::Partition> expected_partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        REQUIRE(gucc::fs::btrfs_append_subvolumes(partitions, {}));
        REQUIRE(partitions.size() == 2);
        REQUIRE(partitions == expected_partitions);
    }
    SECTION("luks xfs")
    {
        const std::vector<gucc::fs::Partition> expected_partitions{
            gucc::fs::Partition{.fstype = "xfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,lazytime,noatime,attr2,inode64,logbsize=256k,noquota"s, .luks_mapper_name = "luks_device"s, .luks_uuid = "00e1b836-81b6-433f-83ca-0fd373e3cd50"s},
            gucc::fs::Partition{.fstype = "linuxswap"s, .mountpoint = ""s, .uuid_str = ""s, .device = "/dev/nvme0n1p3"s, .mount_opts = "defaults,noatime"s},
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "xfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,lazytime,noatime,attr2,inode64,logbsize=256k,noquota"s, .luks_mapper_name = "luks_device"s, .luks_uuid = "00e1b836-81b6-433f-83ca-0fd373e3cd50"s},
            gucc::fs::Partition{.fstype = "linuxswap"s, .mountpoint = ""s, .uuid_str = ""s, .device = "/dev/nvme0n1p3"s, .mount_opts = "defaults,noatime"s},
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        REQUIRE(!gucc::fs::btrfs_append_subvolumes(partitions, subvolumes));
        REQUIRE(partitions == expected_partitions);
    }
    SECTION("valid zfs")
    {
        const std::vector<gucc::fs::Partition> expected_partitions{
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        REQUIRE(!gucc::fs::btrfs_append_subvolumes(partitions, subvolumes));
        REQUIRE(partitions == expected_partitions);
    }
    SECTION("luks btrfs with subvolumes")
    {
        const std::vector<gucc::fs::Partition> expected_partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@home"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@cache"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        REQUIRE(gucc::fs::btrfs_append_subvolumes(partitions, subvolumes));
        REQUIRE(partitions.size() == 4);
        REQUIRE(partitions == expected_partitions);
    }
}
