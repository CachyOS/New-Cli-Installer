#include "doctest_compatibility.h"

#include "gucc/partitioning.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/logger.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

static constexpr auto PART_TEST = R"(label: gpt
type=U,size=2G,bootable
type=L
)"sv;

static constexpr auto PART_BIOS_TEST = R"(label: dos
type=L,size=2G
type=L
)"sv;

static constexpr auto PART_SWAP_TEST = R"(label: gpt
type=U,size=2G,bootable
type=S,size=16G
type=L
)"sv;

static constexpr auto PART_DEFAULT_TEST = R"(label: gpt
type=U,size=4GiB,bootable
type=L
)"sv;

static constexpr auto PART_DEFAULT_BIOS_TEST = R"(label: dos
type=L
)"sv;

static constexpr auto PART_CONFIG_SWAP_TEST = R"(label: gpt
type=S,size=8GiB
type=U,size=1GiB,bootable
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
        const std::vector<gucc::fs::Partition> expected_partitions{
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .device = "/dev/nvme0n1p1"s, .size = "4GiB", .mount_opts = "defaults,umask=0077"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .device = "/dev/nvme0n1p2"s, .mount_opts = "defaults,noatime,compress=zstd:1"s},
        };
        const auto& partitions     = gucc::disk::generate_default_partition_schema("/dev/nvme0n1", "/boot", true);
        const auto& sfdisk_content = gucc::disk::gen_sfdisk_command(partitions, true);
        REQUIRE_EQ(sfdisk_content, PART_DEFAULT_TEST);
        REQUIRE_EQ(partitions, expected_partitions);
    }
    SECTION("default partition schema bios")
    {
        const auto& partitions     = gucc::disk::generate_default_partition_schema("/dev/nvme0n1", "/boot", false);
        const auto& sfdisk_content = gucc::disk::gen_sfdisk_command(partitions, false);
        REQUIRE_EQ(sfdisk_content, PART_DEFAULT_BIOS_TEST);
    }
    // TODO(vnepogodin): add tests for raid and lvm
}

TEST_CASE("partition config test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("filesystem type conversion")
    {
        REQUIRE_EQ(gucc::fs::filesystem_type_to_string(gucc::fs::FilesystemType::Btrfs), "btrfs"sv);
        REQUIRE_EQ(gucc::fs::filesystem_type_to_string(gucc::fs::FilesystemType::Ext4), "ext4"sv);
        REQUIRE_EQ(gucc::fs::filesystem_type_to_string(gucc::fs::FilesystemType::Xfs), "xfs"sv);
        REQUIRE_EQ(gucc::fs::filesystem_type_to_string(gucc::fs::FilesystemType::Vfat), "vfat"sv);
        REQUIRE_EQ(gucc::fs::filesystem_type_to_string(gucc::fs::FilesystemType::LinuxSwap), "swap"sv);
        REQUIRE_EQ(gucc::fs::filesystem_type_to_string(gucc::fs::FilesystemType::Zfs), "zfs"sv);
    }
    SECTION("string to filesystem type")
    {
        REQUIRE_EQ(gucc::fs::string_to_filesystem_type("btrfs"sv), gucc::fs::FilesystemType::Btrfs);
        REQUIRE_EQ(gucc::fs::string_to_filesystem_type("ext4"sv), gucc::fs::FilesystemType::Ext4);
        REQUIRE_EQ(gucc::fs::string_to_filesystem_type("vfat"sv), gucc::fs::FilesystemType::Vfat);
        REQUIRE_EQ(gucc::fs::string_to_filesystem_type("fat32"sv), gucc::fs::FilesystemType::Vfat);
        REQUIRE_EQ(gucc::fs::string_to_filesystem_type("swap"sv), gucc::fs::FilesystemType::LinuxSwap);
        REQUIRE_EQ(gucc::fs::string_to_filesystem_type("linuxswap"sv), gucc::fs::FilesystemType::LinuxSwap);
        REQUIRE_EQ(gucc::fs::string_to_filesystem_type("invalid"sv), gucc::fs::FilesystemType::Unknown);
    }
    SECTION("mount options for btrfs")
    {
        const auto& opts = gucc::fs::get_available_mount_opts(gucc::fs::FilesystemType::Btrfs);
        REQUIRE_FALSE(opts.empty());
        bool has_compress = false;
        for (const auto& opt : opts) {
            if (opt.name == "compress=zstd"s) has_compress = true;
        }
        REQUIRE(has_compress);
    }
    SECTION("mount options for ext4")
    {
        const auto& opts = gucc::fs::get_available_mount_opts(gucc::fs::FilesystemType::Ext4);
        REQUIRE_FALSE(opts.empty());
    }
    SECTION("default mount opts SSD vs HDD")
    {
        const auto& ssd_opts = gucc::fs::get_default_mount_opts(gucc::fs::FilesystemType::Btrfs, true);
        const auto& hdd_opts = gucc::fs::get_default_mount_opts(gucc::fs::FilesystemType::Btrfs, false);
        REQUIRE(ssd_opts.contains("zstd:1"));
        REQUIRE(hdd_opts.contains("zstd"));
        REQUIRE_FALSE(hdd_opts.contains("zstd:1"));
    }
    SECTION("mkfs commands")
    {
        REQUIRE_EQ(gucc::fs::get_mkfs_command(gucc::fs::FilesystemType::Btrfs), "mkfs.btrfs -f"sv);
        REQUIRE_EQ(gucc::fs::get_mkfs_command(gucc::fs::FilesystemType::Ext4), "mkfs.ext4 -q"sv);
        REQUIRE_EQ(gucc::fs::get_mkfs_command(gucc::fs::FilesystemType::Xfs), "mkfs.xfs -f"sv);
        REQUIRE_EQ(gucc::fs::get_mkfs_command(gucc::fs::FilesystemType::LinuxSwap), "mkswap"sv);
    }
    SECTION("sfdisk type aliases")
    {
        REQUIRE_EQ(gucc::fs::get_sfdisk_type_alias(gucc::fs::FilesystemType::Vfat), "U"sv);
        REQUIRE_EQ(gucc::fs::get_sfdisk_type_alias(gucc::fs::FilesystemType::LinuxSwap), "S"sv);
        REQUIRE_EQ(gucc::fs::get_sfdisk_type_alias(gucc::fs::FilesystemType::Btrfs), "L"sv);
        REQUIRE_EQ(gucc::fs::get_sfdisk_type_alias(gucc::fs::FilesystemType::Ext4), "L"sv);
    }
}

TEST_CASE("partition schema from config test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("basic uefi config with swap")
    {
        gucc::fs::DefaultPartitionSchemaConfig config{
            .root_fs_type = gucc::fs::FilesystemType::Ext4,
            .efi_partition_size = "1GiB"s,
            .swap_partition_size = "8GiB"s,
            .is_ssd = true,
            .boot_mountpoint = "/boot"s,
        };
        const auto& partitions = gucc::disk::generate_partition_schema_from_config("/dev/sda"sv, config, true);
        REQUIRE_EQ(partitions.size(), 3);
        REQUIRE_EQ(partitions[0].fstype, "vfat"sv);
        REQUIRE_EQ(partitions[0].mountpoint, "/boot"sv);
        REQUIRE_EQ(partitions[1].fstype, "linuxswap"sv);
        REQUIRE_EQ(partitions[2].fstype, "ext4"sv);
        REQUIRE_EQ(partitions[2].mountpoint, "/"sv);

        const auto& sfdisk_content = gucc::disk::gen_sfdisk_command(partitions, true);
        REQUIRE_EQ(sfdisk_content, PART_CONFIG_SWAP_TEST);
    }
    SECTION("bios config without swap")
    {
        gucc::fs::DefaultPartitionSchemaConfig config{
            .root_fs_type = gucc::fs::FilesystemType::Btrfs,
            .is_ssd = false,
        };
        const auto& partitions = gucc::disk::generate_partition_schema_from_config("/dev/sda"sv, config, false);
        REQUIRE_EQ(partitions.size(), 1);
        REQUIRE_EQ(partitions[0].fstype, "btrfs"sv);
        REQUIRE_EQ(partitions[0].mountpoint, "/"sv);
        REQUIRE(partitions[0].mount_opts.contains("compress=zstd"sv));
    }
    SECTION("default uefi config")
    {
        gucc::fs::DefaultPartitionSchemaConfig config{
            .root_fs_type = gucc::fs::FilesystemType::Btrfs,
            .efi_partition_size = "4GiB"s,
            .is_ssd = true,
            .boot_mountpoint = "/boot"s,
        };
        const auto& partitions = gucc::disk::generate_partition_schema_from_config("/dev/nvme0n1"sv, config, true);
        REQUIRE_EQ(partitions.size(), 2);
        REQUIRE_EQ(partitions[0].fstype, "vfat"sv);
        REQUIRE_EQ(partitions[0].device, "/dev/nvme0n1p1"sv);
        REQUIRE_EQ(partitions[0].mountpoint, "/boot"sv);
        REQUIRE_EQ(partitions[0].mount_opts, "defaults,umask=0077"sv);
        REQUIRE_EQ(partitions[1].fstype, "btrfs"sv);
        REQUIRE_EQ(partitions[1].device, "/dev/nvme0n1p2"sv);
        REQUIRE_EQ(partitions[1].mountpoint, "/"sv);
        REQUIRE_EQ(partitions[1].mount_opts, "defaults,noatime,compress=zstd:1"sv);
    }
}

TEST_CASE("partition schema validation test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("valid uefi schema")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .device = "/dev/sda1"s, .size = "512MiB"s},
            gucc::fs::Partition{.fstype = "ext4"s, .mountpoint = "/"s, .device = "/dev/sda2"s},
        };
        const auto& result = gucc::disk::validate_partition_schema(partitions, "/dev/sda"sv, true);
        REQUIRE(result.is_valid);
        REQUIRE(result.errors.empty());
    }
    SECTION("valid uefi schema btrfs subvolumes")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/home"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@home"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/var/cache"s, .uuid_str = "6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"s, .device = "/dev/nvme0n1p1"s, .mount_opts = "defaults,noatime,compress=zstd,space_cache=v2,commit=120"s, .subvolume = "/@cache"s},
            gucc::fs::Partition{.fstype = "fat32"s, .mountpoint = "/boot"s, .uuid_str = "8EFB-4B84"s, .device = "/dev/nvme0n1p2"s, .size = "2G", .mount_opts = "defaults,noatime"s},
        };
        const auto& result = gucc::disk::validate_partition_schema(partitions, "/dev/nvme0n1"sv, true);
        REQUIRE(result.is_valid);
        REQUIRE(result.errors.empty());
    }
    SECTION("invalid - empty schema")
    {
        const std::vector<gucc::fs::Partition> partitions{};
        const auto& result = gucc::disk::validate_partition_schema(partitions, "/dev/sda"sv, true);
        REQUIRE_FALSE(result.is_valid);
        REQUIRE_FALSE(result.errors.empty());
    }
    SECTION("invalid - no root partition")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .device = "/dev/sda1"s},
        };
        const auto& result = gucc::disk::validate_partition_schema(partitions, "/dev/sda"sv, true);
        REQUIRE_FALSE(result.is_valid);
    }
    SECTION("invalid - uefi without efi partition")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "ext4"s, .mountpoint = "/"s, .device = "/dev/sda1"s},
        };
        const auto& result = gucc::disk::validate_partition_schema(partitions, "/dev/sda"sv, true);
        REQUIRE_FALSE(result.is_valid);
    }
}

TEST_CASE("partition schema preview test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("preview contains device and partitions")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .device = "/dev/sda1"s, .size = "512MiB"s},
            gucc::fs::Partition{.fstype = "ext4"s, .mountpoint = "/"s, .device = "/dev/sda2"s},
        };
        const auto& preview = gucc::disk::preview_partition_schema(partitions, "/dev/sda"sv, true);
        REQUIRE(preview.contains("/dev/sda"sv));
        REQUIRE(preview.contains("UEFI"sv));
        REQUIRE(preview.contains("/dev/sda1"sv));
        REQUIRE(preview.contains("/dev/sda2"sv));
        REQUIRE(preview.contains("vfat"sv));
        REQUIRE(preview.contains("ext4"sv));
        REQUIRE(preview.contains("label: gpt"sv));
    }
    SECTION("preview shows subvolumes")
    {
        const std::vector<gucc::fs::Partition> partitions{
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/"s, .device = "/dev/sda1"s, .subvolume = "/@"s},
            gucc::fs::Partition{.fstype = "btrfs"s, .mountpoint = "/home"s, .device = "/dev/sda1"s, .subvolume = "/@home"s},
            gucc::fs::Partition{.fstype = "vfat"s, .mountpoint = "/boot"s, .device = "/dev/sda2"s, .size = "512MiB"s},
        };
        const auto& preview = gucc::disk::preview_partition_schema(partitions, "/dev/sda"sv, true);
        REQUIRE(preview.contains("Subvolume"sv));
        REQUIRE(preview.contains("/@"sv));
        REQUIRE(preview.contains("/@home"sv));
    }
}

