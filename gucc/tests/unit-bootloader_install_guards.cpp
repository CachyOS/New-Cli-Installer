#include "doctest_compatibility.h"

#include "gucc/bootloader.hpp"
#include "gucc/error.hpp"
#include "gucc/logger.hpp"

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

TEST_CASE("bootloader empty efi path")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("systemd-boot")
    {
        const gucc::bootloader::SystemdBootInstallConfig config{
            .is_removable    = false,
            .root_mountpoint = "/mnt",
            .efi_directory   = "",
        };
        const auto res = gucc::bootloader::install_systemd_boot(config);
        REQUIRE(!res);
        REQUIRE_EQ(res.error().code, gucc::ErrorCode::InvalidArgument);
    }
    SECTION("grub (EFI) with unset efi_directory")
    {
        const gucc::bootloader::GrubConfig grub_config{};
        gucc::bootloader::GrubInstallConfig install_config{};
        install_config.is_efi        = true;
        install_config.efi_directory = std::nullopt;

        const auto res = gucc::bootloader::install_grub(grub_config, install_config, "/mnt");
        REQUIRE(!res);
        REQUIRE_EQ(res.error().code, gucc::ErrorCode::InvalidArgument);
    }
    SECTION("grub (EFI) with empty efi_directory")
    {
        const gucc::bootloader::GrubConfig grub_config{};
        gucc::bootloader::GrubInstallConfig install_config{};
        install_config.is_efi        = true;
        install_config.efi_directory = "";

        const auto res = gucc::bootloader::install_grub(grub_config, install_config, "/mnt");
        REQUIRE(!res);
        REQUIRE_EQ(res.error().code, gucc::ErrorCode::InvalidArgument);
    }
    SECTION("refind")
    {
        const gucc::bootloader::RefindInstallConfig config{
            .is_removable          = false,
            .root_mountpoint       = "/mnt",
            .boot_mountpoint       = "",
            .extra_kernel_versions = {},
            .kernel_params         = {},
        };
        const auto res = gucc::bootloader::install_refind(config);
        REQUIRE(!res);
        REQUIRE_EQ(res.error().code, gucc::ErrorCode::InvalidArgument);
    }
}
