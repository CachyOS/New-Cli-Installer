#include "doctest_compatibility.h"

#include "gucc/bootloader.hpp"
#include "gucc/kernel_params.hpp"
#include "gucc/logger.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

static constexpr auto LIMINE_CONFIG_TEST = R"(KERNEL_CMDLINE[default]="quiet splash rw root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"
)"sv;
static constexpr auto LIMINE_CONFIG_SUBVOL_TEST = R"(KERNEL_CMDLINE[default]="quiet splash rw rootflags=subvol=/@ root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"
)"sv;
static constexpr auto LIMINE_CONFIG_SWAP_TEST = R"(KERNEL_CMDLINE[default]="quiet splash rw root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f resume=UUID=59848b1b-c6be-48f4-b3e1-48179ea72dec"
)"sv;
static constexpr auto LIMINE_CONFIG_LUKS_TEST = R"(KERNEL_CMDLINE[default]="quiet splash rw cryptdevice=UUID=00e1b836-81b6-433f-83ca-0fd373e3cd50:luks_device root=/dev/mapper/luks_device"
)"sv;
static constexpr auto LIMINE_CONFIG_LUKS_SWAP_TEST = R"(KERNEL_CMDLINE[default]="quiet splash rw cryptdevice=UUID=00e1b836-81b6-433f-83ca-0fd373e3cd50:luks_device root=/dev/mapper/luks_device resume=/dev/mapper/luks_swap_device"
)"sv;
static constexpr auto LIMINE_CONFIG_ZFS_TEST = R"(KERNEL_CMDLINE[default]="quiet splash rw root=ZFS=zpcachyos/ROOT root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"
)"sv;
static constexpr auto LIMINE_CONFIG_LUKS_SUBVOL_TEST = R"(KERNEL_CMDLINE[default]="quiet splash rw rootflags=subvol=/@ cryptdevice=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f:luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f root=/dev/mapper/luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"
)"sv;


TEST_CASE("limine config gen test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("btrfs with subvolumes")
    {
        const std::vector<std::string> kernel_params{
            "quiet", "splash", "rw", "rootflags=subvol=/@", "root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"};

        const auto& limine_config_text = gucc::bootloader::gen_limine_config(kernel_params);
        REQUIRE_EQ(limine_config_text, LIMINE_CONFIG_SUBVOL_TEST);
    }
    SECTION("basic xfs")
    {
        const std::vector<std::string> kernel_params{
            "quiet", "splash", "rw", "root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"};
        const auto& limine_config_text = gucc::bootloader::gen_limine_config(kernel_params);
        REQUIRE_EQ(limine_config_text, LIMINE_CONFIG_TEST);
    }
    SECTION("swap xfs")
    {
        const std::vector<std::string> kernel_params{
            "quiet", "splash", "rw", "root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f", "resume=UUID=59848b1b-c6be-48f4-b3e1-48179ea72dec"};
        const auto& limine_config_text = gucc::bootloader::gen_limine_config(kernel_params);
        REQUIRE_EQ(limine_config_text, LIMINE_CONFIG_SWAP_TEST);
    }
    SECTION("luks xfs")
    {
        const std::vector<std::string> kernel_params{
            "quiet", "splash", "rw", "cryptdevice=UUID=00e1b836-81b6-433f-83ca-0fd373e3cd50:luks_device", "root=/dev/mapper/luks_device"};
        const auto& limine_config_text = gucc::bootloader::gen_limine_config(kernel_params);
        REQUIRE_EQ(limine_config_text, LIMINE_CONFIG_LUKS_TEST);
    }
    SECTION("luks swap xfs")
    {
        const std::vector<std::string> kernel_params{
            "quiet", "splash", "rw", "cryptdevice=UUID=00e1b836-81b6-433f-83ca-0fd373e3cd50:luks_device", "root=/dev/mapper/luks_device", "resume=/dev/mapper/luks_swap_device"};
        const auto& limine_config_text = gucc::bootloader::gen_limine_config(kernel_params);
        REQUIRE_EQ(limine_config_text, LIMINE_CONFIG_LUKS_SWAP_TEST);
    }
    SECTION("valid zfs")
    {
        const std::vector<std::string> kernel_params{
            "quiet", "splash", "rw", "root=ZFS=zpcachyos/ROOT", "root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"};
        const auto& limine_config_text = gucc::bootloader::gen_limine_config(kernel_params);
        REQUIRE_EQ(limine_config_text, LIMINE_CONFIG_ZFS_TEST);
    }
    SECTION("luks btrfs with subvolumes")
    {
        const std::vector<std::string> kernel_params{
            "quiet", "splash", "rw", "rootflags=subvol=/@", "cryptdevice=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f:luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f", "root=/dev/mapper/luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"};
        const auto& limine_config_text = gucc::bootloader::gen_limine_config(kernel_params);
        REQUIRE_EQ(limine_config_text, LIMINE_CONFIG_LUKS_SUBVOL_TEST);
    }
}
