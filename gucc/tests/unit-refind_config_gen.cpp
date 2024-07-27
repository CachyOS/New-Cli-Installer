#include "gucc/bootloader.hpp"
#include "gucc/kernel_params.hpp"
#include "gucc/logger.hpp"

#include <cassert>

#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

static constexpr auto REFIND_CONFIG_SUBVOL_TEST = R"("Boot with standard options"    "quiet splash rw rootflags=subvol=/@ root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"
"Boot to single-user mode"    "quiet splash rw rootflags=subvol=/@ root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f" single
)"sv;

static constexpr auto REFIND_CONFIG_TEST = R"("Boot with standard options"    "quiet splash rw root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"
"Boot to single-user mode"    "quiet splash rw root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f" single
)"sv;

static constexpr auto REFIND_CONFIG_SWAP_TEST = R"("Boot with standard options"    "quiet splash rw root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f resume=UUID=59848b1b-c6be-48f4-b3e1-48179ea72dec"
"Boot to single-user mode"    "quiet splash rw root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f resume=UUID=59848b1b-c6be-48f4-b3e1-48179ea72dec" single
)"sv;

static constexpr auto REFIND_CONFIG_LUKS_TEST = R"("Boot with standard options"    "quiet splash rw cryptdevice=UUID=00e1b836-81b6-433f-83ca-0fd373e3cd50:luks_device root=/dev/mapper/luks_device"
"Boot to single-user mode"    "quiet splash rw cryptdevice=UUID=00e1b836-81b6-433f-83ca-0fd373e3cd50:luks_device root=/dev/mapper/luks_device" single
)"sv;

static constexpr auto REFIND_CONFIG_LUKS_SWAP_TEST = R"("Boot with standard options"    "quiet splash rw cryptdevice=UUID=00e1b836-81b6-433f-83ca-0fd373e3cd50:luks_device root=/dev/mapper/luks_device resume=/dev/mapper/luks_swap_device"
"Boot to single-user mode"    "quiet splash rw cryptdevice=UUID=00e1b836-81b6-433f-83ca-0fd373e3cd50:luks_device root=/dev/mapper/luks_device resume=/dev/mapper/luks_swap_device" single
)"sv;

static constexpr auto REFIND_CONFIG_ZFS_TEST = R"("Boot with standard options"    "quiet splash rw root=ZFS=zpcachyos/ROOT root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"
"Boot to single-user mode"    "quiet splash rw root=ZFS=zpcachyos/ROOT root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f" single
)"sv;

static constexpr auto REFIND_CONFIG_LUKS_SUBVOL_TEST = R"("Boot with standard options"    "quiet splash rw rootflags=subvol=/@ cryptdevice=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f:luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f root=/dev/mapper/luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"
"Boot to single-user mode"    "quiet splash rw rootflags=subvol=/@ cryptdevice=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f:luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f root=/dev/mapper/luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f" single
)"sv;

int main() {
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    // btrfs with subvolumes
    {
        const std::vector<std::string> kernel_params{
            "quiet", "splash", "rw", "rootflags=subvol=/@", "root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"};

        const auto& refind_config_text = gucc::bootloader::gen_refind_config(kernel_params);
        assert(refind_config_text == REFIND_CONFIG_SUBVOL_TEST);
    }
    // basic xfs
    {
        const std::vector<std::string> kernel_params{
            "quiet", "splash", "rw", "root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"};
        const auto& refind_config_text = gucc::bootloader::gen_refind_config(kernel_params);
        assert(refind_config_text == REFIND_CONFIG_TEST);
    }
    // swap xfs
    {
        const std::vector<std::string> kernel_params{
            "quiet", "splash", "rw", "root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f", "resume=UUID=59848b1b-c6be-48f4-b3e1-48179ea72dec"};
        const auto& refind_config_text = gucc::bootloader::gen_refind_config(kernel_params);
        assert(refind_config_text == REFIND_CONFIG_SWAP_TEST);
    }
    // luks xfs
    {
        const std::vector<std::string> kernel_params{
            "quiet", "splash", "rw", "cryptdevice=UUID=00e1b836-81b6-433f-83ca-0fd373e3cd50:luks_device", "root=/dev/mapper/luks_device"};
        const auto& refind_config_text = gucc::bootloader::gen_refind_config(kernel_params);
        assert(refind_config_text == REFIND_CONFIG_LUKS_TEST);
    }
    // luks swap xfs
    {
        const std::vector<std::string> kernel_params{
            "quiet", "splash", "rw", "cryptdevice=UUID=00e1b836-81b6-433f-83ca-0fd373e3cd50:luks_device", "root=/dev/mapper/luks_device", "resume=/dev/mapper/luks_swap_device"};
        const auto& refind_config_text = gucc::bootloader::gen_refind_config(kernel_params);
        assert(refind_config_text == REFIND_CONFIG_LUKS_SWAP_TEST);
    }
    // valid zfs
    {
        const std::vector<std::string> kernel_params{
            "quiet", "splash", "rw", "root=ZFS=zpcachyos/ROOT", "root=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"};
        const auto& refind_config_text = gucc::bootloader::gen_refind_config(kernel_params);
        assert(refind_config_text == REFIND_CONFIG_ZFS_TEST);
    }
    // luks btrfs with subvolumes
    {
        const std::vector<std::string> kernel_params{
            "quiet", "splash", "rw", "rootflags=subvol=/@", "cryptdevice=UUID=6bdb3301-8efb-4b84-b0b7-4caeef26fd6f:luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f", "root=/dev/mapper/luks-6bdb3301-8efb-4b84-b0b7-4caeef26fd6f"};
        const auto& refind_config_text = gucc::bootloader::gen_refind_config(kernel_params);
        assert(refind_config_text == REFIND_CONFIG_LUKS_SUBVOL_TEST);
    }
}
