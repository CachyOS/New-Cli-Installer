#include "doctest_compatibility.h"

#include "gucc/partitioning.hpp"
#include "gucc/logger.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

static constexpr auto PART_TEST = R"(label: gpt
type=L
type=U,size=2G,bootable
)"sv;

static constexpr auto PART_BIOS_TEST = R"(label: dos
type=L
type=L,size=2G
)"sv;

static constexpr auto PART_SWAP_TEST = R"(label: gpt
type=L
type=U,size=2G,bootable
type=S,size=16G
)"sv;

static constexpr auto PART_DEFAULT_TEST = R"(label: gpt
type=U,size=2GiB,bootable
type=L
)"sv;

static constexpr auto PART_DEFAULT_BIOS_TEST = R"(label: dos
type=L
)"sv;

TEST_CASE("partitioning gen test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("btrfs with subvolumes")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@home"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@cache"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .size = "2G", .mount_opts = "defaults,noatime"s},
        };
        const auto& sfdisk_content = gucc::disk::gen_sfdisk_command(partitions, true);
        REQUIRE_EQ(sfdisk_content, PART_TEST);
    }
    SECTION("basic xfs")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "xfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,lazytime,noatime,attr2,inode64,logbsize=256k,noquota"s},
            gucc::fs::Partition{.fstype = "fat16"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .size = "2G", .mount_opts = "defaults,noatime"s},
        };
        const auto& sfdisk_content = gucc::disk::gen_sfdisk_command(partitions, true);
        REQUIRE_EQ(sfdisk_content, PART_TEST);
    }
    SECTION("basic xfs bios")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "xfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,lazytime,noatime,attr2,inode64,logbsize=256k,noquota"s},
            gucc::fs::Partition{.fstype = "ext4"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .size = "2G", .mount_opts = "defaults,noatime"s},
        };
        const auto& sfdisk_content = gucc::disk::gen_sfdisk_command(partitions, false);
        REQUIRE_EQ(sfdisk_content, PART_BIOS_TEST);
    }
    SECTION("swap xfs")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "xfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,lazytime,noatime,attr2,inode64,logbsize=256k,noquota"s, .luks_mapper_name = "luks_device"s, .luks_uuid = "00e1b836-81b6-433f-83ca-0fd373e3cd50"s},
            gucc::fs::Partition{.fstype = "linuxswap"s, .mountpoint = ""s, .uuid_str = ""s, .device = "/dev/nvme0n1p3"s, .size = "16G", .mount_opts = "defaults,noatime"s},
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .size = "2G", .mount_opts = "defaults,noatime"s},
        };
        const auto& sfdisk_content = gucc::disk::gen_sfdisk_command(partitions, true);
        REQUIRE_EQ(sfdisk_content, PART_SWAP_TEST);
    }
    SECTION("zfs")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .size = "2G", .mount_opts = "defaults,noatime"s},
        };
        const auto& sfdisk_content = gucc::disk::gen_sfdisk_command(partitions, true);
        REQUIRE_EQ(sfdisk_content, PART_TEST);
    }
    SECTION("default partition schema")
    {
        const auto& partitions     = gucc::disk::generate_default_partition_schema("/dev/nvme0n1p", "/boot", true);
        const auto& sfdisk_content = gucc::disk::gen_sfdisk_command(partitions, true);
        REQUIRE_EQ(sfdisk_content, PART_DEFAULT_TEST);
    }
    SECTION("default partition schema bios")
    {
        const auto& partitions     = gucc::disk::generate_default_partition_schema("/dev/nvme0n1p", "/boot", false);
        const auto& sfdisk_content = gucc::disk::gen_sfdisk_command(partitions, false);
        REQUIRE_EQ(sfdisk_content, PART_DEFAULT_BIOS_TEST);
    }
    // TODO(vnepogodin): add tests for raid and lvm
}
