#include "doctest_compatibility.h"

#include "gucc/block_devices.hpp"

#include <optional>
#include <string>
#include <vector>

using namespace std::string_view_literals;
using namespace std::string_literals;

using gucc::disk::BlockDevice;

namespace {
auto make_device(std::string name, std::string type, std::string fstype = {},
    std::string uuid = {}, std::optional<std::string> pkname = std::nullopt,
    std::optional<std::string> mountpoint = std::nullopt,
    std::optional<std::string> partuuid = std::nullopt) -> BlockDevice {
    BlockDevice dev;
    dev.name       = std::move(name);
    dev.type       = std::move(type);
    dev.fstype     = std::move(fstype);
    dev.uuid       = std::move(uuid);
    dev.pkname     = std::move(pkname);
    dev.mountpoint = std::move(mountpoint);
    dev.partuuid   = std::move(partuuid);
    return dev;
}
}

TEST_CASE("strip_device_prefix test")
{
    using gucc::disk::strip_device_prefix;

    SECTION("/dev/mapper/ prefix")
    {
        CHECK(strip_device_prefix("/dev/mapper/cryptroot"sv) == "cryptroot"sv);
        CHECK(strip_device_prefix("/dev/mapper/luks-abcd-1234"sv) == "luks-abcd-1234"sv);
        CHECK(strip_device_prefix("/dev/mapper/vg0-root"sv) == "vg0-root"sv);
    }
    SECTION("/dev/ prefix")
    {
        CHECK(strip_device_prefix("/dev/sda1"sv) == "sda1"sv);
        CHECK(strip_device_prefix("/dev/nvme0n1p2"sv) == "nvme0n1p2"sv);
        CHECK(strip_device_prefix("/dev/vda"sv) == "vda"sv);
    }
    SECTION("no prefix")
    {
        CHECK(strip_device_prefix("sda1"sv) == "sda1"sv);
        CHECK(strip_device_prefix("cryptroot"sv) == "cryptroot"sv);
        CHECK(strip_device_prefix(""sv) == ""sv);
    }
    SECTION("/dev/mapper/ takes priority over /dev/")
    {
        auto result = strip_device_prefix("/dev/mapper/test"sv);
        CHECK(result == "test"sv);
    }
    SECTION("exact prefix only")
    {
        CHECK(strip_device_prefix("/dev/mapperX"sv) == "mapperX"sv);
        CHECK(strip_device_prefix("/dev/"sv) == ""sv);
        CHECK(strip_device_prefix("/dev/mapper/"sv) == ""sv);
    }
    SECTION("constexpr usage")
    {
        static_assert(strip_device_prefix("/dev/mapper/root"sv) == "root"sv);
        static_assert(strip_device_prefix("/dev/sda1"sv) == "sda1"sv);
        static_assert(strip_device_prefix("nvme0n1"sv) == "nvme0n1"sv);
        CHECK(true);
    }
}

TEST_CASE("find_device_by_name test")
{
    using gucc::disk::find_device_by_name;

    const std::vector<BlockDevice> devices{
        make_device("/dev/sda1", "part", "vfat", "AAAA-BBBB"),
        make_device("/dev/sda2", "part", "ext4", "1111-2222"),
        make_device("/dev/nvme0n1p1", "part", "btrfs", "3333-4444"),
    };

    SECTION("found")
    {
        auto result = find_device_by_name(devices, "/dev/sda2"sv);
        REQUIRE(result.has_value());
        CHECK(result->name == "/dev/sda2"sv);
        CHECK(result->fstype == "ext4"sv);
    }
    SECTION("not found")
    {
        auto result = find_device_by_name(devices, "/dev/sdb1"sv);
        CHECK(!result.has_value());
    }
    SECTION("empty devices")
    {
        auto result = find_device_by_name({}, "/dev/sda1"sv);
        CHECK(!result.has_value());
    }
}

TEST_CASE("find_device_by_pkname test")
{
    using gucc::disk::find_device_by_pkname;

    const std::vector<BlockDevice> devices{
        make_device("/dev/sda1", "part", "vfat", "AAAA", std::nullopt),
        make_device("/dev/mapper/cryptroot", "crypt", "ext4", "BBBB", "/dev/sda2"s),
        make_device("/dev/sda2", "part", "crypto_LUKS", "CCCC", std::nullopt),
    };

    SECTION("found")
    {
        auto result = find_device_by_pkname(devices, "/dev/sda2"sv);
        REQUIRE(result.has_value());
        CHECK(result->name == "/dev/mapper/cryptroot"sv);
    }
    SECTION("not found")
    {
        auto result = find_device_by_pkname(devices, "/dev/sdb1"sv);
        CHECK(!result.has_value());
    }
    SECTION("device without pkname")
    {
        auto result = find_device_by_pkname(devices, "/dev/sda1"sv);
        // sda1 has no pkname, but we're searching for pkname == "/dev/sda1",
        // which no device has as its pkname
        CHECK(!result.has_value());
    }
}

TEST_CASE("find_device_by_mountpoint test")
{
    using gucc::disk::find_device_by_mountpoint;

    const std::vector<BlockDevice> devices{
        make_device("/dev/sda1", "part", "vfat", "AAAA", std::nullopt, "/boot/efi"s),
        make_device("/dev/sda2", "part", "ext4", "BBBB", std::nullopt, "/"s),
        make_device("/dev/sda3", "part", "swap", "CCCC"),
    };

    SECTION("found")
    {
        auto result = find_device_by_mountpoint(devices, "/"sv);
        REQUIRE(result.has_value());
        CHECK(result->name == "/dev/sda2"sv);
    }
    SECTION("boot mountpoint")
    {
        auto result = find_device_by_mountpoint(devices, "/boot/efi"sv);
        REQUIRE(result.has_value());
        CHECK(result->name == "/dev/sda1"sv);
    }
    SECTION("not found - unmounted device")
    {
        auto result = find_device_by_mountpoint(devices, "/mnt"sv);
        CHECK(!result.has_value());
    }
    SECTION("empty devices")
    {
        auto result = find_device_by_mountpoint({}, "/"sv);
        CHECK(!result.has_value());
    }
}

TEST_CASE("has_type_in_ancestry test")
{
    using gucc::disk::has_type_in_ancestry;

    SECTION("direct crypt device")
    {
        // /dev/mapper/cryptroot is type=crypt, parent is /dev/sda2 (type=part)
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda2", "part", "crypto_LUKS", "AAAA"),
            make_device("/dev/mapper/cryptroot", "crypt", "ext4", "BBBB", "/dev/sda2"s, "/"s),
        };

        CHECK(has_type_in_ancestry(devices, "/dev/mapper/cryptroot"sv, "crypt"sv));
        CHECK(!has_type_in_ancestry(devices, "/dev/sda2"sv, "crypt"sv));
        CHECK(has_type_in_ancestry(devices, "/dev/sda2"sv, "part"sv));
    }
    SECTION("LVM-on-LUKS chain")
    {
        // /dev/sda2 (part) -> /dev/mapper/cryptlvm (crypt) -> /dev/mapper/vg-root (lvm)
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda2", "part", "crypto_LUKS", "AAAA"),
            make_device("/dev/mapper/cryptlvm", "crypt", "LVM2_member", "BBBB", "/dev/sda2"s),
            make_device("/dev/mapper/vg-root", "lvm", "ext4", "CCCC", "/dev/mapper/cryptlvm"s, "/mnt"s),
        };

        // vg-root -> cryptlvm (crypt) -> sda2 (part)
        CHECK(has_type_in_ancestry(devices, "/dev/mapper/vg-root"sv, "lvm"sv));
        CHECK(has_type_in_ancestry(devices, "/dev/mapper/vg-root"sv, "crypt"sv));
        CHECK(has_type_in_ancestry(devices, "/dev/mapper/vg-root"sv, "part"sv));
        CHECK(!has_type_in_ancestry(devices, "/dev/mapper/vg-root"sv, "disk"sv));
    }
    SECTION("no ancestry match")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda1", "part", "ext4", "AAAA"),
        };
        CHECK(!has_type_in_ancestry(devices, "/dev/sda1"sv, "crypt"sv));
        CHECK(!has_type_in_ancestry(devices, "/dev/sda1"sv, "lvm"sv));
    }
    SECTION("device not found")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda1", "part", "ext4", "AAAA"),
        };
        CHECK(!has_type_in_ancestry(devices, "/dev/nonexistent"sv, "part"sv));
    }
    SECTION("empty devices")
    {
        CHECK(!has_type_in_ancestry({}, "/dev/sda1"sv, "part"sv));
    }
    SECTION("cycle prevention")
    {
        // A -> B -> A
        const std::vector<BlockDevice> devices{
            make_device("/dev/A", "part", "ext4", "AAAA", "/dev/B"s),
            make_device("/dev/B", "crypt", "ext4", "BBBB", "/dev/A"s),
        };
        // Should terminate without infinite loop
        CHECK(has_type_in_ancestry(devices, "/dev/A"sv, "part"sv));
        CHECK(has_type_in_ancestry(devices, "/dev/A"sv, "crypt"sv));
        CHECK(!has_type_in_ancestry(devices, "/dev/A"sv, "lvm"sv));
    }
}

TEST_CASE("find_ancestor_of_type test")
{
    using gucc::disk::find_ancestor_of_type;

    SECTION("find crypt ancestor")
    {
        // /dev/sda2 (part) -> /dev/mapper/cryptlvm (crypt) -> /dev/mapper/vg-root (lvm)
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda2", "part", "crypto_LUKS", "AAAA"),
            make_device("/dev/mapper/cryptlvm", "crypt", "LVM2_member", "BBBB", "/dev/sda2"s),
            make_device("/dev/mapper/vg-root", "lvm", "ext4", "CCCC", "/dev/mapper/cryptlvm"s, "/mnt"s),
        };

        auto result = find_ancestor_of_type(devices, "/dev/mapper/vg-root"sv, "crypt"sv);
        REQUIRE(result.has_value());
        CHECK(result->name == "/dev/mapper/cryptlvm"sv);
    }
    SECTION("find part ancestor through crypt")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda2", "part", "crypto_LUKS", "AAAA"),
            make_device("/dev/mapper/cryptlvm", "crypt", "LVM2_member", "BBBB", "/dev/sda2"s),
            make_device("/dev/mapper/vg-root", "lvm", "ext4", "CCCC", "/dev/mapper/cryptlvm"s, "/mnt"s),
        };

        auto result = find_ancestor_of_type(devices, "/dev/mapper/vg-root"sv, "part"sv);
        REQUIRE(result.has_value());
        CHECK(result->name == "/dev/sda2"sv);
    }
    SECTION("device itself matches type")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/mapper/cryptroot", "crypt", "ext4", "AAAA", "/dev/sda2"s),
            make_device("/dev/sda2", "part", "crypto_LUKS", "BBBB"),
        };

        auto result = find_ancestor_of_type(devices, "/dev/mapper/cryptroot"sv, "crypt"sv);
        REQUIRE(result.has_value());
        CHECK(result->name == "/dev/mapper/cryptroot"sv);
    }
    SECTION("no ancestor of type found")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda1", "part", "ext4", "AAAA"),
        };

        auto result = find_ancestor_of_type(devices, "/dev/sda1"sv, "crypt"sv);
        CHECK(!result.has_value());
    }
    SECTION("device not found")
    {
        auto result = find_ancestor_of_type({}, "/dev/nonexistent"sv, "part"sv);
        CHECK(!result.has_value());
    }
    SECTION("returns UUID from ancestor")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda2", "part", "crypto_LUKS", "underlying-uuid-1234", std::nullopt, std::nullopt, "partuuid-5678"s),
            make_device("/dev/mapper/cryptroot", "crypt", "ext4", "crypt-uuid", "/dev/sda2"s, "/mnt"s),
        };

        auto result = find_ancestor_of_type(devices, "/dev/mapper/cryptroot"sv, "part"sv);
        REQUIRE(result.has_value());
        CHECK(result->uuid == "underlying-uuid-1234"sv);
        CHECK(result->partuuid.value() == "partuuid-5678"sv);
    }
}

TEST_CASE("find_devices_by_type test")
{
    using gucc::disk::find_devices_by_type;

    const std::vector<BlockDevice> devices{
        make_device("/dev/sda1", "part", "vfat", "AAAA"),
        make_device("/dev/sda2", "part", "ext4", "BBBB"),
        make_device("/dev/mapper/cryptroot", "crypt", "ext4", "CCCC", "/dev/sda2"s),
        make_device("/dev/mapper/vg-root", "lvm", "ext4", "DDDD", "/dev/mapper/cryptroot"s),
        make_device("/dev/mapper/vg-home", "lvm", "ext4", "EEEE", "/dev/mapper/cryptroot"s),
    };

    SECTION("find partitions")
    {
        auto result = find_devices_by_type(devices, "part"sv);
        REQUIRE(result.size() == 2);
        CHECK(result[0].name == "/dev/sda1"sv);
        CHECK(result[1].name == "/dev/sda2"sv);
    }
    SECTION("find lvm devices")
    {
        auto result = find_devices_by_type(devices, "lvm"sv);
        REQUIRE(result.size() == 2);
        CHECK(result[0].name == "/dev/mapper/vg-root"sv);
        CHECK(result[1].name == "/dev/mapper/vg-home"sv);
    }
    SECTION("find crypt devices")
    {
        auto result = find_devices_by_type(devices, "crypt"sv);
        REQUIRE(result.size() == 1);
        CHECK(result[0].name == "/dev/mapper/cryptroot"sv);
    }
    SECTION("no match")
    {
        auto result = find_devices_by_type(devices, "disk"sv);
        CHECK(result.empty());
    }
    SECTION("empty devices")
    {
        auto result = find_devices_by_type({}, "part"sv);
        CHECK(result.empty());
    }
}

TEST_CASE("find_devices_by_type_and_fstype test")
{
    using gucc::disk::find_devices_by_type_and_fstype;

    const std::vector<BlockDevice> devices{
        make_device("/dev/sda1", "part", "vfat", "AAAA"),
        make_device("/dev/sda2", "part", "crypto_LUKS", "BBBB"),
        make_device("/dev/mapper/cryptroot", "crypt", "LVM2_member", "CCCC", "/dev/sda2"s),
        make_device("/dev/mapper/vg-root", "lvm", "ext4", "DDDD", "/dev/mapper/cryptroot"s),
        make_device("/dev/mapper/vg-swap", "lvm", "swap", "EEEE", "/dev/mapper/cryptroot"s),
        make_device("/dev/sdb1", "part", "ext4", "FFFF"),
    };

    SECTION("find part with crypto_LUKS")
    {
        auto result = find_devices_by_type_and_fstype(devices, "part"sv, "crypto_LUKS"sv);
        REQUIRE(result.size() == 1);
        CHECK(result[0].name == "/dev/sda2"sv);
    }
    SECTION("find lvm with ext4")
    {
        auto result = find_devices_by_type_and_fstype(devices, "lvm"sv, "ext4"sv);
        REQUIRE(result.size() == 1);
        CHECK(result[0].name == "/dev/mapper/vg-root"sv);
    }
    SECTION("find lvm with swap")
    {
        auto result = find_devices_by_type_and_fstype(devices, "lvm"sv, "swap"sv);
        REQUIRE(result.size() == 1);
        CHECK(result[0].name == "/dev/mapper/vg-swap"sv);
    }
    SECTION("case-insensitive fstype matching")
    {
        auto result = find_devices_by_type_and_fstype(devices, "part"sv, "CRYPTO_LUKS"sv);
        REQUIRE(result.size() == 1);
        CHECK(result[0].name == "/dev/sda2"sv);

        auto result2 = find_devices_by_type_and_fstype(devices, "part"sv, "crypto_luks"sv);
        REQUIRE(result2.size() == 1);
        CHECK(result2[0].name == "/dev/sda2"sv);
    }
    SECTION("no match - type mismatch")
    {
        auto result = find_devices_by_type_and_fstype(devices, "crypt"sv, "ext4"sv);
        CHECK(result.empty());
    }
    SECTION("no match - fstype mismatch")
    {
        auto result = find_devices_by_type_and_fstype(devices, "part"sv, "btrfs"sv);
        CHECK(result.empty());
    }
    SECTION("empty devices")
    {
        auto result = find_devices_by_type_and_fstype({}, "part"sv, "ext4"sv);
        CHECK(result.empty());
    }
}

TEST_CASE("block device helpers - LUKS")
{
    using namespace gucc::disk;

    // /dev/sda1 (EFI) + /dev/sda2 (LUKS) -> /dev/mapper/cryptroot (ext4, mounted /mnt)
    const std::vector<BlockDevice> devices{
        make_device("/dev/sda1", "part", "vfat", "efi-uuid", std::nullopt, "/mnt/boot/efi"s, "efi-partuuid"s),
        make_device("/dev/sda2", "part", "crypto_LUKS", "luks-uuid", std::nullopt, std::nullopt, "luks-partuuid"s),
        make_device("/dev/mapper/cryptroot", "crypt", "ext4", "root-uuid", "/dev/sda2"s, "/mnt"s),
    };

    SECTION("find root device by mountpoint and strip prefix")
    {
        auto mnt_dev = find_device_by_mountpoint(devices, "/mnt"sv);
        REQUIRE(mnt_dev.has_value());
        CHECK(mnt_dev->name == "/dev/mapper/cryptroot"sv);
        CHECK(strip_device_prefix(mnt_dev->name) == "cryptroot"sv);
    }
    SECTION("check root is on crypt")
    {
        auto mnt_dev = find_device_by_mountpoint(devices, "/mnt"sv);
        REQUIRE(mnt_dev.has_value());
        CHECK(has_type_in_ancestry(devices, mnt_dev->name, "crypt"sv));
    }
    SECTION("find underlying partition UUID")
    {
        auto mnt_dev = find_device_by_mountpoint(devices, "/mnt"sv);
        REQUIRE(mnt_dev.has_value());
        auto part = find_ancestor_of_type(devices, mnt_dev->name, "part"sv);
        REQUIRE(part.has_value());
        CHECK(part->uuid == "luks-uuid"sv);
        CHECK(part->name == "/dev/sda2"sv);
    }
}

TEST_CASE("block device helpers - LVM-on-LUKS")
{
    using namespace gucc::disk;

    // /dev/sda2 (LUKS) -> /dev/mapper/cryptlvm (crypt, LVM2_member) -> /dev/mapper/vg-root (lvm, ext4)
    const std::vector<BlockDevice> devices{
        make_device("/dev/sda1", "part", "vfat", "efi-uuid", std::nullopt, "/mnt/boot/efi"s),
        make_device("/dev/sda2", "part", "crypto_LUKS", "luks-uuid"),
        make_device("/dev/mapper/cryptlvm", "crypt", "LVM2_member", "crypt-uuid", "/dev/sda2"s),
        make_device("/dev/mapper/vg-root", "lvm", "ext4", "root-uuid", "/dev/mapper/cryptlvm"s, "/mnt"s),
        make_device("/dev/mapper/vg-home", "lvm", "ext4", "home-uuid", "/dev/mapper/cryptlvm"s, "/mnt/home"s),
    };

    SECTION("root is on lvm")
    {
        auto mnt_dev = find_device_by_mountpoint(devices, "/mnt"sv);
        REQUIRE(mnt_dev.has_value());
        CHECK(mnt_dev->type == "lvm"sv);
    }
    SECTION("root has crypt in ancestry")
    {
        auto mnt_dev = find_device_by_mountpoint(devices, "/mnt"sv);
        REQUIRE(mnt_dev.has_value());
        CHECK(has_type_in_ancestry(devices, mnt_dev->name, "crypt"sv));
        CHECK(has_type_in_ancestry(devices, mnt_dev->name, "part"sv));
    }
    SECTION("find crypt ancestor from lvm device")
    {
        auto crypt = find_ancestor_of_type(devices, "/dev/mapper/vg-root"sv, "crypt"sv);
        REQUIRE(crypt.has_value());
        CHECK(crypt->name == "/dev/mapper/cryptlvm"sv);
        CHECK(strip_device_prefix(crypt->name) == "cryptlvm"sv);
    }
    SECTION("find part ancestor from lvm device")
    {
        auto part = find_ancestor_of_type(devices, "/dev/mapper/vg-root"sv, "part"sv);
        REQUIRE(part.has_value());
        CHECK(part->name == "/dev/sda2"sv);
        CHECK(part->uuid == "luks-uuid"sv);
    }
    SECTION("no LUKS-on-LVM devices present")
    {
        auto luks_on_lvm = find_devices_by_type_and_fstype(devices, "lvm"sv, "crypto_LUKS"sv);
        CHECK(luks_on_lvm.empty());
    }
}

TEST_CASE("block device helpers - LUKS-on-LVM")
{
    using namespace gucc::disk;

    // /dev/sda2 (LVM2_member) -> /dev/mapper/vg-root (lvm, crypto_LUKS) -> /dev/mapper/cryptroot (crypt, ext4)
    const std::vector<BlockDevice> devices{
        make_device("/dev/sda1", "part", "vfat", "efi-uuid", std::nullopt, "/mnt/boot"s),
        make_device("/dev/sda2", "part", "LVM2_member", "pv-uuid"),
        make_device("/dev/mapper/vg-root", "lvm", "crypto_LUKS", "lvm-uuid", "/dev/sda2"s),
        make_device("/dev/mapper/cryptroot", "crypt", "ext4", "root-uuid", "/dev/mapper/vg-root"s, "/mnt"s),
    };

    SECTION("detect LUKS-on-LVM pattern")
    {
        auto luks_on_lvm = find_devices_by_type_and_fstype(devices, "lvm"sv, "crypto_LUKS"sv);
        REQUIRE(luks_on_lvm.size() == 1);
        CHECK(luks_on_lvm[0].name == "/dev/mapper/vg-root"sv);
    }
    SECTION("root has both crypt and lvm in ancestry")
    {
        auto mnt_dev = find_device_by_mountpoint(devices, "/mnt"sv);
        REQUIRE(mnt_dev.has_value());
        CHECK(has_type_in_ancestry(devices, mnt_dev->name, "crypt"sv));
        CHECK(has_type_in_ancestry(devices, mnt_dev->name, "lvm"sv));
    }
}

TEST_CASE("block device helpers - plain partition")
{
    using namespace gucc::disk;

    const std::vector<BlockDevice> devices{
        make_device("/dev/sda1", "part", "vfat", "efi-uuid", std::nullopt, "/mnt/boot/efi"s),
        make_device("/dev/sda2", "part", "ext4", "root-uuid", std::nullopt, "/mnt"s, "root-partuuid"s),
        make_device("/dev/sda3", "part", "swap", "swap-uuid"),
    };

    SECTION("root is not on crypt or lvm")
    {
        auto mnt_dev = find_device_by_mountpoint(devices, "/mnt"sv);
        REQUIRE(mnt_dev.has_value());
        CHECK(mnt_dev->type == "part"sv);
        CHECK(!has_type_in_ancestry(devices, mnt_dev->name, "crypt"sv));
        CHECK(!has_type_in_ancestry(devices, mnt_dev->name, "lvm"sv));
    }
    SECTION("strip prefix from plain partition")
    {
        CHECK(strip_device_prefix("/dev/sda2"sv) == "sda2"sv);
    }
    SECTION("partuuid accessible from found device")
    {
        auto dev = find_device_by_name(devices, "/dev/sda2"sv);
        REQUIRE(dev.has_value());
        REQUIRE(dev->partuuid.has_value());
        CHECK(dev->partuuid.value() == "root-partuuid"sv);
    }
}
