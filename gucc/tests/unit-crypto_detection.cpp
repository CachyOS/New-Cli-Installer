#include "doctest_compatibility.h"

#include "gucc/crypto_detection.hpp"
#include "gucc/logger.hpp"

#include <optional>
#include <string>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

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
}  // namespace

TEST_CASE("detect_crypto_for_device test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("LUKS alone")
    {
        // part(crypto_LUKS) -> crypt(ext4)
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda1", "part", "vfat", "efi-uuid", std::nullopt, "/mnt/boot/efi"s),
            make_device("/dev/sda2", "part", "crypto_LUKS", "luks-uuid-1234"),
            make_device("/dev/mapper/cryptroot", "crypt", "ext4", "root-uuid", "/dev/sda2"s, "/mnt"s),
        };

        auto result = gucc::disk::detect_crypto_for_device(devices, "/dev/mapper/cryptroot"sv);
        REQUIRE(result.has_value());
        CHECK(result->is_luks == true);
        CHECK(result->is_lvm == false);
        CHECK(result->luks_mapper_name == "cryptroot"sv);
        CHECK(result->luks_uuid == "luks-uuid-1234"sv);
        CHECK(result->luks_dev == "cryptdevice=UUID=luks-uuid-1234:cryptroot"sv);
    }
    SECTION("LVM-on-LUKS")
    {
        // part(crypto_LUKS) -> crypt(LVM2_member) -> lvm(ext4)
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda1", "part", "vfat", "efi-uuid", std::nullopt, "/mnt/boot/efi"s),
            make_device("/dev/sda2", "part", "crypto_LUKS", "luks-uuid-5678"),
            make_device("/dev/mapper/cryptlvm", "crypt", "LVM2_member", "crypt-uuid", "/dev/sda2"s),
            make_device("/dev/mapper/vg-root", "lvm", "ext4", "root-uuid", "/dev/mapper/cryptlvm"s, "/mnt"s),
        };

        auto result = gucc::disk::detect_crypto_for_device(devices, "/dev/mapper/vg-root"sv);
        REQUIRE(result.has_value());
        CHECK(result->is_luks == true);
        CHECK(result->is_lvm == true);
        CHECK(result->luks_mapper_name == "cryptlvm"sv);
        CHECK(result->luks_uuid == "luks-uuid-5678"sv);
        CHECK(result->luks_dev == "cryptdevice=UUID=luks-uuid-5678:cryptlvm"sv);
    }
    SECTION("LUKS-on-LVM")
    {
        // part(LVM2_member) -> lvm(crypto_LUKS) -> crypt(ext4)
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda1", "part", "vfat", "efi-uuid", std::nullopt, "/mnt/boot"s),
            make_device("/dev/sda2", "part", "LVM2_member", "pv-uuid"),
            make_device("/dev/mapper/vg-root", "lvm", "crypto_LUKS", "lvm-uuid", "/dev/sda2"s),
            make_device("/dev/mapper/cryptroot", "crypt", "ext4", "root-uuid", "/dev/mapper/vg-root"s, "/mnt"s),
        };

        auto result = gucc::disk::detect_crypto_for_device(devices, "/dev/mapper/cryptroot"sv);
        REQUIRE(result.has_value());
        CHECK(result->is_luks == true);
        CHECK(result->is_lvm == true);
        CHECK(result->luks_mapper_name == "cryptroot"sv);
        CHECK(result->luks_dev == "cryptdevice=/dev/mapper/vg-root:cryptroot"sv);
    }
    SECTION("plain partition")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda1", "part", "vfat", "efi-uuid", std::nullopt, "/mnt/boot/efi"s),
            make_device("/dev/sda2", "part", "ext4", "root-uuid", std::nullopt, "/mnt"s),
        };

        auto result = gucc::disk::detect_crypto_for_device(devices, "/dev/sda2"sv);
        REQUIRE(result.has_value());
        CHECK(result->is_luks == false);
        CHECK(result->is_lvm == false);
        CHECK(result->luks_mapper_name.empty());
        CHECK(result->luks_dev.empty());
        CHECK(result->luks_uuid.empty());
    }
    SECTION("device not found")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda1", "part", "ext4", "uuid"),
        };

        auto result = gucc::disk::detect_crypto_for_device(devices, "/dev/nonexistent"sv);
        CHECK(!result.has_value());
    }
}

TEST_CASE("detect_crypto_for_mountpoint test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    const std::vector<BlockDevice> devices{
        make_device("/dev/sda2", "part", "crypto_LUKS", "luks-uuid"),
        make_device("/dev/mapper/cryptroot", "crypt", "ext4", "root-uuid", "/dev/sda2"s, "/mnt"s),
    };

    SECTION("found")
    {
        auto result = gucc::disk::detect_crypto_for_mountpoint(devices, "/mnt"sv);
        REQUIRE(result.has_value());
        CHECK(result->is_luks == true);
        CHECK(result->luks_mapper_name == "cryptroot"sv);
    }
    SECTION("mountpoint not found")
    {
        auto result = gucc::disk::detect_crypto_for_mountpoint(devices, "/nonexistent"sv);
        CHECK(!result.has_value());
    }
}

TEST_CASE("detect_crypto_for_boot test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("encrypted boot - LUKS alone")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda1", "part", "crypto_LUKS", "boot-luks-uuid"),
            make_device("/dev/mapper/cryptboot", "crypt", "vfat", "boot-uuid", "/dev/sda1"s, "/mnt/boot"s),
            make_device("/dev/sda2", "part", "ext4", "root-uuid", std::nullopt, "/mnt"s),
        };

        auto result = gucc::disk::detect_crypto_for_boot(devices, "/mnt/boot"sv);
        REQUIRE(result.has_value());
        CHECK(result->is_luks == true);
        CHECK(result->is_lvm == false);
        CHECK(result->luks_mapper_name == "cryptboot"sv);
        CHECK(result->luks_uuid == "boot-luks-uuid"sv);
        CHECK(result->luks_dev == "cryptdevice=UUID=boot-luks-uuid:cryptboot"sv);
    }
    SECTION("encrypted boot on LVM")
    {
        // boot is on lvm which is on crypt
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda2", "part", "crypto_LUKS", "luks-uuid"),
            make_device("/dev/mapper/cryptlvm", "crypt", "LVM2_member", "crypt-uuid", "/dev/sda2"s),
            make_device("/dev/mapper/vg-boot", "lvm", "vfat", "boot-uuid", "/dev/mapper/cryptlvm"s, "/mnt/boot"s),
            make_device("/dev/mapper/vg-root", "lvm", "ext4", "root-uuid", "/dev/mapper/cryptlvm"s, "/mnt"s),
        };

        auto result = gucc::disk::detect_crypto_for_boot(devices, "/mnt/boot"sv);
        REQUIRE(result.has_value());
        CHECK(result->is_luks == true);
        CHECK(result->is_lvm == true);
        CHECK(result->luks_mapper_name == "cryptlvm"sv);
        CHECK(result->luks_uuid == "luks-uuid"sv);
    }
    SECTION("unencrypted boot")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda1", "part", "vfat", "efi-uuid", std::nullopt, "/mnt/boot/efi"s),
            make_device("/dev/sda2", "part", "ext4", "root-uuid", std::nullopt, "/mnt"s),
        };

        auto result = gucc::disk::detect_crypto_for_boot(devices, "/mnt/boot/efi"sv);
        REQUIRE(result.has_value());
        CHECK(result->is_luks == false);
    }
    SECTION("boot not found")
    {
        auto result = gucc::disk::detect_crypto_for_boot({}, "/mnt/boot"sv);
        CHECK(!result.has_value());
    }
}

TEST_CASE("is_encrypted test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("root encrypted, no boot")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda2", "part", "crypto_LUKS", "luks-uuid"),
            make_device("/dev/mapper/cryptroot", "crypt", "ext4", "root-uuid", "/dev/sda2"s, "/mnt"s),
        };
        CHECK(gucc::disk::is_encrypted(devices, "/mnt"sv, ""sv) == true);
    }
    SECTION("root encrypted, boot unencrypted")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda1", "part", "vfat", "efi-uuid", std::nullopt, "/mnt/boot"s),
            make_device("/dev/sda2", "part", "crypto_LUKS", "luks-uuid"),
            make_device("/dev/mapper/cryptroot", "crypt", "ext4", "root-uuid", "/dev/sda2"s, "/mnt"s),
        };
        CHECK(gucc::disk::is_encrypted(devices, "/mnt"sv, "/mnt/boot"sv) == true);
    }
    SECTION("root unencrypted, boot encrypted")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda1", "part", "crypto_LUKS", "boot-luks-uuid"),
            make_device("/dev/mapper/cryptboot", "crypt", "vfat", "boot-uuid", "/dev/sda1"s, "/mnt/boot"s),
            make_device("/dev/sda2", "part", "ext4", "root-uuid", std::nullopt, "/mnt"s),
        };
        CHECK(gucc::disk::is_encrypted(devices, "/mnt"sv, "/mnt/boot"sv) == true);
    }
    SECTION("nothing encrypted")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda1", "part", "vfat", "efi-uuid", std::nullopt, "/mnt/boot"s),
            make_device("/dev/sda2", "part", "ext4", "root-uuid", std::nullopt, "/mnt"s),
        };
        CHECK(gucc::disk::is_encrypted(devices, "/mnt"sv, "/mnt/boot"sv) == false);
    }
    SECTION("empty devices")
    {
        CHECK(gucc::disk::is_encrypted({}, "/mnt"sv, ""sv) == false);
    }
}

TEST_CASE("is_fde test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("separate boot encrypted - FDE")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda1", "part", "crypto_LUKS", "boot-luks-uuid"),
            make_device("/dev/mapper/cryptboot", "crypt", "vfat", "boot-uuid", "/dev/sda1"s, "/mnt/boot"s),
            make_device("/dev/sda2", "part", "crypto_LUKS", "luks-uuid"),
            make_device("/dev/mapper/cryptroot", "crypt", "ext4", "root-uuid", "/dev/sda2"s, "/mnt"s),
        };
        CHECK(gucc::disk::is_fde(devices, "/mnt"sv, "/mnt/boot"sv, true) == true);
    }
    SECTION("separate boot not encrypted - not FDE")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda1", "part", "vfat", "efi-uuid", std::nullopt, "/mnt/boot"s),
            make_device("/dev/sda2", "part", "crypto_LUKS", "luks-uuid"),
            make_device("/dev/mapper/cryptroot", "crypt", "ext4", "root-uuid", "/dev/sda2"s, "/mnt"s),
        };
        CHECK(gucc::disk::is_fde(devices, "/mnt"sv, "/mnt/boot"sv, true) == false);
    }
    SECTION("no separate boot, root encrypted via luks_flag - FDE")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda2", "part", "crypto_LUKS", "luks-uuid"),
            make_device("/dev/mapper/cryptroot", "crypt", "ext4", "root-uuid", "/dev/sda2"s, "/mnt"s),
        };
        CHECK(gucc::disk::is_fde(devices, "/mnt"sv, ""sv, true) == true);
    }
    SECTION("no separate boot, root encrypted via ancestry - FDE")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda2", "part", "crypto_LUKS", "luks-uuid"),
            make_device("/dev/mapper/cryptroot", "crypt", "ext4", "root-uuid", "/dev/sda2"s, "/mnt"s),
        };
        CHECK(gucc::disk::is_fde(devices, "/mnt"sv, ""sv, false) == true);
    }
    SECTION("no separate boot, not encrypted - not FDE")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda2", "part", "ext4", "root-uuid", std::nullopt, "/mnt"s),
        };
        CHECK(gucc::disk::is_fde(devices, "/mnt"sv, ""sv, false) == false);
    }
}

TEST_CASE("list_mounted_devices test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    const std::vector<BlockDevice> devices{
        make_device("/dev/sda1", "part", "vfat", "efi-uuid", std::nullopt, "/mnt/boot/efi"s),
        make_device("/dev/sda2", "part", "ext4", "root-uuid", std::nullopt, "/mnt"s),
        make_device("/dev/sda3", "part", "ext4", "home-uuid", std::nullopt, "/mnt/home"s),
        make_device("/dev/sdb1", "part", "ext4", "other-uuid", std::nullopt, "/other"s),
        make_device("/dev/sdb2", "part", "swap", "swap-uuid"),
    };

    SECTION("multiple mounts under /mnt")
    {
        auto result = gucc::disk::list_mounted_devices(devices, "/mnt"sv);
        REQUIRE(result.size() == 3);
        CHECK(result[0] == "/dev/sda1"sv);
        CHECK(result[1] == "/dev/sda2"sv);
        CHECK(result[2] == "/dev/sda3"sv);
    }
    SECTION("single mount")
    {
        auto result = gucc::disk::list_mounted_devices(devices, "/other"sv);
        REQUIRE(result.size() == 1);
        CHECK(result[0] == "/dev/sdb1"sv);
    }
    SECTION("empty result")
    {
        auto result = gucc::disk::list_mounted_devices(devices, "/nonexistent"sv);
        CHECK(result.empty());
    }
    SECTION("empty devices")
    {
        auto result = gucc::disk::list_mounted_devices({}, "/mnt"sv);
        CHECK(result.empty());
    }
}

TEST_CASE("find_underlying_partition test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("LVM chain")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda2", "part", "crypto_LUKS", "part-uuid"),
            make_device("/dev/mapper/cryptlvm", "crypt", "LVM2_member", "crypt-uuid", "/dev/sda2"s),
            make_device("/dev/mapper/vg-root", "lvm", "ext4", "root-uuid", "/dev/mapper/cryptlvm"s, "/mnt"s),
        };

        auto result = gucc::disk::find_underlying_partition(devices, "/mnt"sv);
        REQUIRE(result.has_value());
        CHECK(result->name == "/dev/sda2"sv);
        CHECK(result->uuid == "part-uuid"sv);
    }
    SECTION("direct partition")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda2", "part", "ext4", "root-uuid", std::nullopt, "/mnt"s),
        };

        auto result = gucc::disk::find_underlying_partition(devices, "/mnt"sv);
        REQUIRE(result.has_value());
        CHECK(result->name == "/dev/sda2"sv);
    }
    SECTION("LUKS alone")
    {
        const std::vector<BlockDevice> devices{
            make_device("/dev/sda2", "part", "crypto_LUKS", "part-uuid"),
            make_device("/dev/mapper/cryptroot", "crypt", "ext4", "root-uuid", "/dev/sda2"s, "/mnt"s),
        };

        auto result = gucc::disk::find_underlying_partition(devices, "/mnt"sv);
        REQUIRE(result.has_value());
        CHECK(result->name == "/dev/sda2"sv);
        CHECK(result->uuid == "part-uuid"sv);
    }
    SECTION("mountpoint not found")
    {
        auto result = gucc::disk::find_underlying_partition({}, "/mnt"sv);
        CHECK(!result.has_value());
    }
}
