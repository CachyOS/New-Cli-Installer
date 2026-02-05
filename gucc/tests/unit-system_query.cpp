#include "doctest_compatibility.h"

#include "gucc/system_query.hpp"
#include "gucc/logger.hpp"

#include <string>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

TEST_CASE("disk transport conversion test")
{
    using gucc::disk::DiskTransport;
    using gucc::disk::disk_transport_to_string;
    using gucc::disk::string_to_disk_transport;

    SECTION("transport to string")
    {
        CHECK(disk_transport_to_string(DiskTransport::Sata) == "sata"sv);
        CHECK(disk_transport_to_string(DiskTransport::Nvme) == "nvme"sv);
        CHECK(disk_transport_to_string(DiskTransport::Usb) == "usb"sv);
        CHECK(disk_transport_to_string(DiskTransport::Scsi) == "scsi"sv);
        CHECK(disk_transport_to_string(DiskTransport::Virtio) == "virtio"sv);
        CHECK(disk_transport_to_string(DiskTransport::Unknown) == "unknown"sv);
    }
    SECTION("string to transport")
    {
        CHECK(string_to_disk_transport("sata"sv) == DiskTransport::Sata);
        CHECK(string_to_disk_transport("ata"sv) == DiskTransport::Sata);
        CHECK(string_to_disk_transport("nvme"sv) == DiskTransport::Nvme);
        CHECK(string_to_disk_transport("usb"sv) == DiskTransport::Usb);
        CHECK(string_to_disk_transport("scsi"sv) == DiskTransport::Scsi);
        CHECK(string_to_disk_transport("sas"sv) == DiskTransport::Scsi);
        CHECK(string_to_disk_transport("virtio"sv) == DiskTransport::Virtio);
        CHECK(string_to_disk_transport("unknown"sv) == DiskTransport::Unknown);
        CHECK(string_to_disk_transport(""sv) == DiskTransport::Unknown);
    }
}

// NOTE: simply just testing that it compiles as of now
TEST_CASE("is_device_ssd test")
{
    using gucc::disk::is_device_ssd;

    SECTION("handles /dev/ prefix")
    {
        [[maybe_unused]] auto result1 = is_device_ssd("/dev/sda"sv);
        [[maybe_unused]] auto result2 = is_device_ssd("sda"sv);
        CHECK(true);
    }
    SECTION("handles nvme partition suffix")
    {
        [[maybe_unused]] auto result1 = is_device_ssd("/dev/nvme0n1"sv);
        [[maybe_unused]] auto result2 = is_device_ssd("/dev/nvme0n1p1"sv);
        [[maybe_unused]] auto result3 = is_device_ssd("nvme0n1p2"sv);
        CHECK(true);
    }
    SECTION("handles regular disk partition suffix")
    {
        [[maybe_unused]] auto result1 = is_device_ssd("/dev/sda"sv);
        [[maybe_unused]] auto result2 = is_device_ssd("/dev/sda1"sv);
        [[maybe_unused]] auto result3 = is_device_ssd("sdb12"sv);
        [[maybe_unused]] auto result4 = is_device_ssd("vda1"sv);
        CHECK(true);
    }
    SECTION("nvme devices fallback to SSD")
    {
        auto result = is_device_ssd("nvme99n99"sv);
        CHECK(result == true);
    }
    SECTION("virtual devices fallback to SSD")
    {
        auto result = is_device_ssd("vd99"sv);
        CHECK(result == true);
    }
}

TEST_CASE("format size test")
{
    using gucc::disk::format_size;

    SECTION("bytes")
    {
        CHECK(format_size(0) == "0B"sv);
        CHECK(format_size(500) == "500B"sv);
        CHECK(format_size(1023) == "1023B"sv);
    }
    SECTION("kibibytes")
    {
        CHECK(format_size(1024) == "1KiB"sv);
        CHECK(format_size(2048) == "2KiB"sv);
        CHECK(format_size(512 * 1024) == "512KiB"sv);
    }
    SECTION("mebibytes")
    {
        CHECK(format_size(1024ULL * 1024) == "1MiB"sv);
        CHECK(format_size(512ULL * 1024 * 1024) == "512MiB"sv);
    }
    SECTION("gibibytes")
    {
        CHECK(format_size(1024ULL * 1024 * 1024) == "1.0GiB"sv);
        CHECK(format_size(500ULL * 1024 * 1024 * 1024) == "500.0GiB"sv);
    }
    SECTION("tebibytes")
    {
        CHECK(format_size(1024ULL * 1024 * 1024 * 1024) == "1.0TiB"sv);
        CHECK(format_size(2ULL * 1024 * 1024 * 1024 * 1024) == "2.0TiB"sv);
    }
}

TEST_CASE("parse_partition_number test")
{
    using gucc::disk::parse_partition_number;

    SECTION("regular disk partitions")
    {
        CHECK(parse_partition_number("sda1"sv) == 1);
        CHECK(parse_partition_number("sda2"sv) == 2);
        CHECK(parse_partition_number("sdb12"sv) == 12);
        CHECK(parse_partition_number("vda1"sv) == 1);
    }
    SECTION("nvme partitions")
    {
        CHECK(parse_partition_number("nvme0n1p1"sv) == 1);
        CHECK(parse_partition_number("nvme0n1p2"sv) == 2);
        CHECK(parse_partition_number("nvme1n1p10"sv) == 10);
    }
    SECTION("full device paths")
    {
        CHECK(parse_partition_number("/dev/sda1"sv) == 1);
        CHECK(parse_partition_number("/dev/nvme0n1p2"sv) == 2);
    }
    SECTION("no partition number")
    {
        CHECK(parse_partition_number("sda"sv) == 0);
        CHECK(parse_partition_number("nvme0n1"sv) == 0);
        CHECK(parse_partition_number("/dev/sda"sv) == 0);
        CHECK(parse_partition_number("/dev/nvme0n1"sv) == 0);
    }
}

TEST_CASE("get_disk_name_from_device test")
{
    using gucc::disk::get_disk_name_from_device;

    SECTION("regular disks with /dev/ prefix")
    {
        CHECK(get_disk_name_from_device("/dev/sda"sv) == "sda"sv);
        CHECK(get_disk_name_from_device("/dev/sda1"sv) == "sda"sv);
        CHECK(get_disk_name_from_device("/dev/sdb12"sv) == "sdb"sv);
        CHECK(get_disk_name_from_device("/dev/vda1"sv) == "vda"sv);
    }
    SECTION("regular disks without /dev/ prefix")
    {
        CHECK(get_disk_name_from_device("sda"sv) == "sda"sv);
        CHECK(get_disk_name_from_device("sda1"sv) == "sda"sv);
        CHECK(get_disk_name_from_device("sdb12"sv) == "sdb"sv);
    }
    SECTION("nvme disks with /dev/ prefix")
    {
        CHECK(get_disk_name_from_device("/dev/nvme0n1"sv) == "nvme0n1"sv);
        CHECK(get_disk_name_from_device("/dev/nvme0n1p1"sv) == "nvme0n1"sv);
        CHECK(get_disk_name_from_device("/dev/nvme0n1p2"sv) == "nvme0n1"sv);
        CHECK(get_disk_name_from_device("/dev/nvme1n1p10"sv) == "nvme1n1"sv);
    }
    SECTION("nvme disks without /dev/ prefix")
    {
        CHECK(get_disk_name_from_device("nvme0n1"sv) == "nvme0n1"sv);
        CHECK(get_disk_name_from_device("nvme0n1p1"sv) == "nvme0n1"sv);
        CHECK(get_disk_name_from_device("nvme0n1p2"sv) == "nvme0n1"sv);
    }
    SECTION("virtio disks")
    {
        CHECK(get_disk_name_from_device("/dev/vda"sv) == "vda"sv);
        CHECK(get_disk_name_from_device("/dev/vda1"sv) == "vda"sv);
        CHECK(get_disk_name_from_device("vdb2"sv) == "vdb"sv);
    }
}

TEST_CASE("parse_lsblk_disks_json test")
{
    using gucc::disk::parse_lsblk_disks_json;
    using gucc::disk::DiskTransport;

    SECTION("empty json output")
    {
        auto disks = parse_lsblk_disks_json(""sv);
        CHECK(disks.empty());
    }
    SECTION("empty blockdevices array")
    {
        constexpr auto json = R"({"blockdevices":[]})"sv;
        auto disks          = parse_lsblk_disks_json(json);
        CHECK(disks.empty());
    }
    SECTION("single disk without partitions")
    {
        constexpr auto json = R"({
            "blockdevices": [
                {
                    "name": "/dev/sda",
                    "type": "disk",
                    "size": 500107862016,
                    "model": "Samsung SSD 860",
                    "fstype": null,
                    "label": null,
                    "uuid": null,
                    "partuuid": null,
                    "mountpoint": null,
                    "pttype": "gpt",
                    "rm": false,
                    "ro": false,
                    "tran": "sata"
                }
            ]
        })"sv;

        auto disks = parse_lsblk_disks_json(json);
        REQUIRE(disks.size() == 1);

        const auto& disk = disks[0];
        CHECK(disk.device == "/dev/sda"sv);
        CHECK(disk.model.has_value());
        CHECK(disk.model.value() == "Samsung SSD 860"sv);
        CHECK(disk.size == 500107862016ULL);
        CHECK(disk.pttype.has_value());
        CHECK(disk.pttype.value() == "gpt"sv);
        CHECK(disk.is_removable == false);
        CHECK(disk.transport == DiskTransport::Sata);
        CHECK(disk.partitions.empty());
    }
    SECTION("disk with partitions")
    {
        constexpr auto json = R"({
            "blockdevices": [
                {
                    "name": "/dev/nvme0n1",
                    "type": "disk",
                    "size": 1000204886016,
                    "model": "Samsung SSD 980 PRO",
                    "fstype": null,
                    "label": null,
                    "uuid": null,
                    "partuuid": null,
                    "mountpoint": null,
                    "pttype": "gpt",
                    "rm": false,
                    "ro": false,
                    "tran": "nvme",
                    "children": [
                        {
                            "name": "/dev/nvme0n1p1",
                            "type": "part",
                            "size": 536870912,
                            "model": null,
                            "fstype": "vfat",
                            "label": "EFI",
                            "uuid": "ABCD-1234",
                            "partuuid": "12345678-1234-1234-1234-123456789abc",
                            "mountpoint": "/boot/efi",
                            "pttype": null,
                            "rm": false,
                            "ro": false,
                            "tran": null
                        },
                        {
                            "name": "/dev/nvme0n1p2",
                            "type": "part",
                            "size": 999667998720,
                            "model": null,
                            "fstype": "btrfs",
                            "label": "CachyOS",
                            "uuid": "87654321-4321-4321-4321-210987654321",
                            "partuuid": "87654321-1234-1234-1234-123456789def",
                            "mountpoint": "/",
                            "pttype": null,
                            "rm": false,
                            "ro": false,
                            "tran": null
                        }
                    ]
                }
            ]
        })"sv;

        auto disks = parse_lsblk_disks_json(json);
        REQUIRE(disks.size() == 1);

        const auto& disk = disks[0];
        CHECK(disk.device == "/dev/nvme0n1"sv);
        CHECK(disk.model.value() == "Samsung SSD 980 PRO"sv);
        CHECK(disk.size == 1000204886016ULL);
        CHECK(disk.transport == DiskTransport::Nvme);
        CHECK(disk.pttype.value() == "gpt"sv);
        REQUIRE(disk.partitions.size() == 2);

        const auto& p1 = disk.partitions[0];
        CHECK(p1.device == "/dev/nvme0n1p1"sv);
        CHECK(p1.fstype == "vfat"sv);
        CHECK(p1.label.value() == "EFI"sv);
        CHECK(p1.uuid.value() == "ABCD-1234"sv);
        CHECK(p1.partuuid.value() == "12345678-1234-1234-1234-123456789abc"sv);
        CHECK(p1.size == 536870912ULL);
        CHECK(p1.mountpoint.value() == "/boot/efi"sv);
        CHECK(p1.is_mounted == true);
        CHECK(p1.part_number == 1);

        const auto& p2 = disk.partitions[1];
        CHECK(p2.device == "/dev/nvme0n1p2"sv);
        CHECK(p2.fstype == "btrfs"sv);
        CHECK(p2.label.value() == "CachyOS"sv);
        CHECK(p2.uuid.value() == "87654321-4321-4321-4321-210987654321"sv);
        CHECK(p2.mountpoint.value() == "/"sv);
        CHECK(p2.is_mounted == true);
        CHECK(p2.part_number == 2);
    }
    SECTION("multiple disks")
    {
        constexpr auto json = R"({
            "blockdevices": [
                {
                    "name": "/dev/sda",
                    "type": "disk",
                    "size": 500107862016,
                    "model": "Samsung SSD 860",
                    "fstype": null,
                    "label": null,
                    "uuid": null,
                    "partuuid": null,
                    "mountpoint": null,
                    "pttype": "gpt",
                    "rm": false,
                    "ro": false,
                    "tran": "sata"
                },
                {
                    "name": "/dev/sdb",
                    "type": "disk",
                    "size": 2000398934016,
                    "model": "WD Blue HDD",
                    "fstype": null,
                    "label": null,
                    "uuid": null,
                    "partuuid": null,
                    "mountpoint": null,
                    "pttype": "gpt",
                    "rm": false,
                    "ro": false,
                    "tran": "sata"
                }
            ]
        })"sv;

        auto disks = parse_lsblk_disks_json(json);
        REQUIRE(disks.size() == 2);
        CHECK(disks[0].device == "/dev/sda"sv);
        CHECK(disks[0].model.value() == "Samsung SSD 860"sv);
        CHECK(disks[1].device == "/dev/sdb"sv);
        CHECK(disks[1].model.value() == "WD Blue HDD"sv);
    }
    SECTION("removable USB disk")
    {
        constexpr auto json = R"({
            "blockdevices": [
                {
                    "name": "/dev/sdc",
                    "type": "disk",
                    "size": 32010928128,
                    "model": "USB Flash Drive",
                    "fstype": null,
                    "label": null,
                    "uuid": null,
                    "partuuid": null,
                    "mountpoint": null,
                    "pttype": "dos",
                    "rm": true,
                    "ro": false,
                    "tran": "usb"
                }
            ]
        })"sv;

        auto disks = parse_lsblk_disks_json(json);
        REQUIRE(disks.size() == 1);

        const auto& disk = disks[0];
        CHECK(disk.device == "/dev/sdc"sv);
        CHECK(disk.is_removable == true);
        CHECK(disk.transport == DiskTransport::Usb);
        CHECK(disk.pttype.value() == "dos"sv);
    }
    SECTION("virtio disk")
    {
        constexpr auto json = R"({
            "blockdevices": [
                {
                    "name": "/dev/vda",
                    "type": "disk",
                    "size": 53687091200,
                    "model": null,
                    "fstype": null,
                    "label": null,
                    "uuid": null,
                    "partuuid": null,
                    "mountpoint": null,
                    "pttype": "gpt",
                    "rm": false,
                    "ro": false,
                    "tran": "virtio"
                }
            ]
        })"sv;

        auto disks = parse_lsblk_disks_json(json);
        REQUIRE(disks.size() == 1);

        const auto& disk = disks[0];
        CHECK(disk.device == "/dev/vda"sv);
        CHECK(!disk.model.has_value());
        CHECK(disk.transport == DiskTransport::Virtio);
    }
    SECTION("mixed device types - only disks returned")
    {
        constexpr auto json = R"({
            "blockdevices": [
                {
                    "name": "/dev/sda",
                    "type": "disk",
                    "size": 500107862016,
                    "model": "Samsung SSD 860",
                    "fstype": null,
                    "label": null,
                    "uuid": null,
                    "partuuid": null,
                    "mountpoint": null,
                    "pttype": "gpt",
                    "rm": false,
                    "ro": false,
                    "tran": "sata"
                },
                {
                    "name": "/dev/sr0",
                    "type": "rom",
                    "size": 0,
                    "model": "DVD-RW Drive",
                    "fstype": null,
                    "label": null,
                    "uuid": null,
                    "partuuid": null,
                    "mountpoint": null,
                    "pttype": null,
                    "rm": true,
                    "ro": false,
                    "tran": "sata"
                },
                {
                    "name": "/dev/loop0",
                    "type": "loop",
                    "size": 12345678,
                    "model": null,
                    "fstype": "squashfs",
                    "label": null,
                    "uuid": null,
                    "partuuid": null,
                    "mountpoint": "/snap/core/1234",
                    "pttype": null,
                    "rm": false,
                    "ro": true,
                    "tran": null
                }
            ]
        })"sv;

        auto disks = parse_lsblk_disks_json(json);
        REQUIRE(disks.size() == 1);
        CHECK(disks[0].device == "/dev/sda"sv);
        CHECK(disks[0].model.value() == "Samsung SSD 860"sv);
    }
    SECTION("partition without mountpoint")
    {
        constexpr auto json = R"({
            "blockdevices": [
                {
                    "name": "/dev/sda",
                    "type": "disk",
                    "size": 500107862016,
                    "model": "Test Disk",
                    "fstype": null,
                    "label": null,
                    "uuid": null,
                    "partuuid": null,
                    "mountpoint": null,
                    "pttype": "gpt",
                    "rm": false,
                    "ro": false,
                    "tran": "sata",
                    "children": [
                        {
                            "name": "/dev/sda1",
                            "type": "part",
                            "size": 250053869568,
                            "model": null,
                            "fstype": "ext4",
                            "label": "DataPartition",
                            "uuid": "11111111-1111-1111-1111-111111111111",
                            "partuuid": "22222222-2222-2222-2222-222222222222",
                            "mountpoint": null,
                            "pttype": null,
                            "rm": false,
                            "ro": false,
                            "tran": null
                        }
                    ]
                }
            ]
        })"sv;

        auto disks = parse_lsblk_disks_json(json);
        REQUIRE(disks.size() == 1);
        REQUIRE(disks[0].partitions.size() == 1);

        const auto& part = disks[0].partitions[0];
        CHECK(part.device == "/dev/sda1"sv);
        CHECK(!part.mountpoint.has_value());
        CHECK(part.is_mounted == false);
        CHECK(part.part_number == 1);
    }
    SECTION("transport detection fallback from device name")
    {
        constexpr auto json = R"({
            "blockdevices": [
                {
                    "name": "/dev/nvme1n1",
                    "type": "disk",
                    "size": 512110190592,
                    "model": "NVMe Drive",
                    "fstype": null,
                    "label": null,
                    "uuid": null,
                    "partuuid": null,
                    "mountpoint": null,
                    "pttype": "gpt",
                    "rm": false,
                    "ro": false,
                    "tran": null
                }
            ]
        })"sv;

        auto disks = parse_lsblk_disks_json(json);
        REQUIRE(disks.size() == 1);
        CHECK(disks[0].transport == DiskTransport::Nvme);
    }
}

