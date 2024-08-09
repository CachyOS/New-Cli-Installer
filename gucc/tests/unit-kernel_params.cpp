#include "doctest_compatibility.h"

#include "gucc/kernel_params.hpp"
#include "gucc/logger.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST_CASE("kernel params get test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    static constexpr auto DEFAULT_KERNEL_PARAMS = "quiet splash"sv;

    SECTION("btrfs with subvolumes")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@home"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@cache"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& kernel_params = gucc::fs::get_kernel_params(partitions, DEFAULT_KERNEL_PARAMS);
        REQUIRE(kernel_params.has_value());
        REQUIRE_EQ(kernel_params->size(), 5);
        REQUIRE_EQ(*kernel_params, std::vector<std::string>{"quiet", "splash", "rw", "rootflags=subvol=/@", "root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"});
    }
    SECTION("invalid xfs (empty uuid)")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "xfs"s, .mountpoint = "/"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,lazytime,noatime,attr2,inode64,logbsize=256k,noquota"s},
            gucc::fs::Partition{.fstype = "fat16"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& kernel_params = gucc::fs::get_kernel_params(partitions, DEFAULT_KERNEL_PARAMS);
        REQUIRE(!kernel_params.has_value());
    }
    SECTION("invalid xfs (without root partition)")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "xfs"s, .mountpoint = "/home"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,lazytime,noatime,attr2,inode64,logbsize=256k,noquota"s},
            gucc::fs::Partition{.fstype = "fat16"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& kernel_params = gucc::fs::get_kernel_params(partitions, DEFAULT_KERNEL_PARAMS);
        REQUIRE(!kernel_params.has_value());
    }
    SECTION("basic xfs")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "xfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,lazytime,noatime,attr2,inode64,logbsize=256k,noquota"s},
            gucc::fs::Partition{.fstype = "fat16"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& kernel_params = gucc::fs::get_kernel_params(partitions, DEFAULT_KERNEL_PARAMS);
        REQUIRE(kernel_params.has_value());
        REQUIRE_EQ(kernel_params->size(), 4);
        REQUIRE_EQ(*kernel_params, std::vector<std::string>{"quiet", "splash", "rw", "root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"});
    }
    SECTION("swap xfs")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "xfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,lazytime,noatime,attr2,inode64,logbsize=256k,noquota"s},
            gucc::fs::Partition{.fstype = "linuxswap"s, .mountpoint = ""s, .uuid_str = "59848b1b-c6be-48f4-b3e1-48179ea72dec"s, .device = "/dev/nvme0n1p3"s, .mount_opts = "defaults,noatime"s},
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& kernel_params = gucc::fs::get_kernel_params(partitions, DEFAULT_KERNEL_PARAMS);
        REQUIRE(kernel_params.has_value());
        REQUIRE_EQ(kernel_params->size(), 5);
        REQUIRE_EQ(*kernel_params, std::vector<std::string>{"quiet", "splash", "rw", "root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f", "resume=UUID=59848b1b-c6be-48f4-b3e1-48179ea72dec"});
    }
    SECTION("luks xfs")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "xfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,lazytime,noatime,attr2,inode64,logbsize=256k,noquota"s, .luks_mapper_name = "luks_device"s, .luks_uuid = "00e1b836-81b6-433f-83ca-0fd373e3cd50"s},
            gucc::fs::Partition{.fstype = "linuxswap"s, .mountpoint = ""s, .uuid_str = ""s, .device = "/dev/nvme0n1p3"s, .mount_opts = "defaults,noatime"s},
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& kernel_params = gucc::fs::get_kernel_params(partitions, DEFAULT_KERNEL_PARAMS);
        REQUIRE(kernel_params.has_value());
        REQUIRE_EQ(kernel_params->size(), 5);
        REQUIRE_EQ(*kernel_params, std::vector<std::string>{"quiet", "splash", "rw", "cryptdevice=UUID=00e1b836-81b6-433f-83ca-0fd373e3cd50:luks_device", "root=/dev/mapper/luks_device"});
    }
    SECTION("luks swap xfs")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "xfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,lazytime,noatime,attr2,inode64,logbsize=256k,noquota"s, .luks_mapper_name = "luks_device"s, .luks_uuid = "00e1b836-81b6-433f-83ca-0fd373e3cd50"s},
            gucc::fs::Partition{.fstype = "linuxswap"s, .mountpoint = ""s, .uuid_str = ""s, .device = "/dev/nvme0n1p3"s, .mount_opts = "defaults,noatime"s, .luks_mapper_name = "luks_swap_device"s, .luks_uuid = "59848b1b-c6be-48f4-b3e1-48179ea72dec"s},
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& kernel_params = gucc::fs::get_kernel_params(partitions, DEFAULT_KERNEL_PARAMS);
        REQUIRE(kernel_params.has_value());
        REQUIRE_EQ(kernel_params->size(), 6);
        REQUIRE_EQ(*kernel_params, std::vector<std::string>{"quiet", "splash", "rw", "cryptdevice=UUID=00e1b836-81b6-433f-83ca-0fd373e3cd50:luks_device", "root=/dev/mapper/luks_device", "resume=/dev/mapper/luks_swap_device"});
    }
    SECTION("invalid zfs")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& kernel_params = gucc::fs::get_kernel_params(partitions, DEFAULT_KERNEL_PARAMS);
        REQUIRE(!kernel_params.has_value());
    }
    SECTION("valid zfs")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "zfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s},
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& kernel_params = gucc::fs::get_kernel_params(partitions, DEFAULT_KERNEL_PARAMS, "zpcachyos/ROOT");
        REQUIRE(kernel_params.has_value());
        REQUIRE_EQ(kernel_params->size(), 5);
        REQUIRE_EQ(*kernel_params, std::vector<std::string>{"quiet", "splash", "rw", "root=ZFS=zpcachyos/ROOT", "root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"});
    }
    SECTION("luks btrfs with subvolumes")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@home"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@cache"s, .luks_mapper_name = "luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .luks_uuid = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime"s},
        };
        const auto& kernel_params = gucc::fs::get_kernel_params(partitions, DEFAULT_KERNEL_PARAMS);
        REQUIRE(kernel_params.has_value());
        REQUIRE_EQ(kernel_params->size(), 6);
        REQUIRE_EQ(*kernel_params, std::vector<std::string>{"quiet", "splash", "rw", "rootflags=subvol=/@", "cryptdevice=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f:luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f", "root=/dev/mapper/luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"});
    }
}
