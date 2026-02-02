#include "doctest_compatibility.h"

#include "gucc/systemd_repart.hpp"
#include "gucc/partition.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/logger.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

TEST_CASE("encrypt mode to string test")
{
    REQUIRE_EQ(gucc::disk::encrypt_mode_to_string(gucc::disk::RepartEncryptMode::Off), "off"sv);
    REQUIRE_EQ(gucc::disk::encrypt_mode_to_string(gucc::disk::RepartEncryptMode::KeyFile), "key-file"sv);
    REQUIRE_EQ(gucc::disk::encrypt_mode_to_string(gucc::disk::RepartEncryptMode::Tpm2), "tpm2"sv);
    REQUIRE_EQ(gucc::disk::encrypt_mode_to_string(gucc::disk::RepartEncryptMode::KeyFilePlusTpm2), "key-file+tpm2"sv);
}

TEST_CASE("empty mode to string test")
{
    REQUIRE_EQ(gucc::disk::empty_mode_to_string(gucc::disk::RepartEmptyMode::Refuse), "refuse"sv);
    REQUIRE_EQ(gucc::disk::empty_mode_to_string(gucc::disk::RepartEmptyMode::Allow), "allow"sv);
    REQUIRE_EQ(gucc::disk::empty_mode_to_string(gucc::disk::RepartEmptyMode::Require), "require"sv);
    REQUIRE_EQ(gucc::disk::empty_mode_to_string(gucc::disk::RepartEmptyMode::Force), "force"sv);
    REQUIRE_EQ(gucc::disk::empty_mode_to_string(gucc::disk::RepartEmptyMode::Create), "create"sv);
}

TEST_CASE("get repart type test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("swap partition")
    {
        REQUIRE_EQ(gucc::disk::get_repart_type(gucc::fs::FilesystemType::LinuxSwap, ""sv), "swap");
    }

    SECTION("ESP partitions")
    {
        REQUIRE_EQ(gucc::disk::get_repart_type(gucc::fs::FilesystemType::Vfat, "/boot"sv), "esp");
        REQUIRE_EQ(gucc::disk::get_repart_type(gucc::fs::FilesystemType::Vfat, "/boot/efi"sv), "esp");
        REQUIRE_EQ(gucc::disk::get_repart_type(gucc::fs::FilesystemType::Vfat, "/efi"sv), "esp");
    }

    SECTION("root partition")
    {
        REQUIRE_EQ(gucc::disk::get_repart_type(gucc::fs::FilesystemType::Btrfs, "/"sv), "root");
        REQUIRE_EQ(gucc::disk::get_repart_type(gucc::fs::FilesystemType::Ext4, "/"sv), "root");
        REQUIRE_EQ(gucc::disk::get_repart_type(gucc::fs::FilesystemType::Xfs, "/"sv), "root");
    }

    SECTION("home partition")
    {
        REQUIRE_EQ(gucc::disk::get_repart_type(gucc::fs::FilesystemType::Ext4, "/home"sv), "home");
    }

    SECTION("srv partition")
    {
        REQUIRE_EQ(gucc::disk::get_repart_type(gucc::fs::FilesystemType::Ext4, "/srv"sv), "srv");
    }

    SECTION("var partition")
    {
        REQUIRE_EQ(gucc::disk::get_repart_type(gucc::fs::FilesystemType::Ext4, "/var"sv), "var");
    }

    SECTION("tmp partition")
    {
        REQUIRE_EQ(gucc::disk::get_repart_type(gucc::fs::FilesystemType::Ext4, "/tmp"sv), "tmp");
        REQUIRE_EQ(gucc::disk::get_repart_type(gucc::fs::FilesystemType::Ext4, "/var/tmp"sv), "tmp");
    }

    SECTION("generic partition")
    {
        REQUIRE_EQ(gucc::disk::get_repart_type(gucc::fs::FilesystemType::Ext4, "/data"sv), "linux-generic");
    }
}

TEST_CASE("generate repart config content test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("minimal ESP config")
    {
        gucc::disk::RepartPartitionConfig config{
            .type = "esp",
            .size_min = "1G",
            .size_max = "1G",
            .format = "vfat",
        };

        const auto content = gucc::disk::generate_repart_config_content(config);

        REQUIRE(content.contains("[Partition]"));
        REQUIRE(content.contains("Type=esp"));
        REQUIRE(content.contains("SizeMinBytes=1G"));
        REQUIRE(content.contains("SizeMaxBytes=1G"));
        REQUIRE(content.contains("Format=vfat"));
        REQUIRE(content.contains("GrowFileSystem=yes"));
    }

    SECTION("root with encryption")
    {
        gucc::disk::RepartPartitionConfig config{
            .type = "root",
            .size_min = "20G",
            .format = "btrfs",
            .encrypt = gucc::disk::RepartEncryptMode::Tpm2,
        };

        const auto content = gucc::disk::generate_repart_config_content(config);

        REQUIRE(content.contains("[Partition]"));
        REQUIRE(content.contains("Type=root"));
        REQUIRE(content.contains("SizeMinBytes=20G"));
        REQUIRE(content.contains("Format=btrfs"));
        REQUIRE(content.contains("Encrypt=tpm2"));
    }

    SECTION("swap partition")
    {
        gucc::disk::RepartPartitionConfig config{
            .type = "swap",
            .size_min = "4G",
            .size_max = "4G",
            .format = "swap",
            .priority = 1,
        };

        const auto content = gucc::disk::generate_repart_config_content(config);

        REQUIRE(content.contains("[Partition]"));
        REQUIRE(content.contains("Type=swap"));
        REQUIRE(content.contains("SizeMinBytes=4G"));
        REQUIRE(content.contains("SizeMaxBytes=4G"));
        REQUIRE(content.contains("Format=swap"));
        REQUIRE(content.contains("Priority=1"));
    }

    SECTION("with label and weight")
    {
        gucc::disk::RepartPartitionConfig config{
            .type = "home",
            .label = "home-partition",
            .format = "ext4",
            .weight = 2000,
        };

        const auto content = gucc::disk::generate_repart_config_content(config);

        REQUIRE(content.contains("Type=home"));
        REQUIRE(content.contains("Label=home-partition"));
        REQUIRE(content.contains("Format=ext4"));
        REQUIRE(content.contains("Weight=2000"));
    }

    SECTION("btrfs with subvolumes")
    {
        gucc::disk::RepartPartitionConfig config{
            .type = "root",
            .format = "btrfs",
            .subvolumes = {"/@", "/@home", "/@cache"},
            .default_subvolume = "/@",
        };

        const auto content = gucc::disk::generate_repart_config_content(config);

        REQUIRE(content.contains("Type=root"));
        REQUIRE(content.contains("Format=btrfs"));
        REQUIRE(content.contains("Subvolumes=/@ /@home /@cache"));
        REQUIRE(content.contains("DefaultSubvolume=/@"));
    }
}

TEST_CASE("convert partition to repart test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("EFI partition")
    {
        gucc::fs::Partition partition{
            .fstype = "vfat",
            .mountpoint = "/boot",
            .device = "/dev/sda1",
            .size = "1G",
        };

        const auto config = gucc::disk::convert_partition_to_repart(partition);

        REQUIRE_EQ(config.type, "esp");
        REQUIRE(config.format.has_value());
        REQUIRE_EQ(*config.format, "vfat");
        REQUIRE(config.size_min.has_value());
        REQUIRE_EQ(*config.size_min, "1G");
    }

    SECTION("root partition with btrfs")
    {
        gucc::fs::Partition partition{
            .fstype = "btrfs",
            .mountpoint = "/",
            .device = "/dev/sda2",
            .size = "50G",
        };

        const auto config = gucc::disk::convert_partition_to_repart(partition);

        REQUIRE_EQ(config.type, "root");
        REQUIRE(config.format.has_value());
        REQUIRE_EQ(*config.format, "btrfs");
    }

    SECTION("swap partition")
    {
        gucc::fs::Partition partition{
            .fstype = "linuxswap",
            .mountpoint = "",
            .device = "/dev/sda3",
            .size = "8G",
        };

        const auto config = gucc::disk::convert_partition_to_repart(partition);

        REQUIRE_EQ(config.type, "swap");
        REQUIRE(config.format.has_value());
        REQUIRE_EQ(*config.format, "swap");
    }

    SECTION("encrypted partition")
    {
        gucc::fs::Partition partition{
            .fstype = "btrfs",
            .mountpoint = "/",
            .device = "/dev/sda2",
            .size = "50G",
            .luks_mapper_name = "cryptroot",
        };

        const auto config = gucc::disk::convert_partition_to_repart(partition);

        REQUIRE_EQ(config.type, "root");
        REQUIRE_EQ(config.encrypt, gucc::disk::RepartEncryptMode::KeyFile);
    }

    SECTION("home partition with label")
    {
        gucc::fs::Partition partition{
            .fstype = "ext4",
            .mountpoint = "/home",
            .device = "/dev/sda4",
        };

        const auto config = gucc::disk::convert_partition_to_repart(partition);

        REQUIRE_EQ(config.type, "home");
        REQUIRE(config.label.has_value());
        REQUIRE_EQ(*config.label, "home");
    }
}

TEST_CASE("convert partitions to repart test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("typical EFI system")
    {
        std::vector<gucc::fs::Partition> partitions{
            {.fstype = "vfat", .mountpoint = "/boot", .device = "/dev/sda1", .size = "1G"},
            {.fstype = "btrfs", .mountpoint = "/", .device = "/dev/sda2", .size = "50G"},
            {.fstype = "linuxswap", .mountpoint = "", .device = "/dev/sda3", .size = "8G"},
        };

        const auto configs = gucc::disk::convert_partitions_to_repart(partitions);

        REQUIRE_EQ(configs.size(), 3);
        REQUIRE_EQ(configs[0].type, "esp");
        REQUIRE_EQ(configs[1].type, "root");
        REQUIRE_EQ(configs[2].type, "swap");
    }
}
