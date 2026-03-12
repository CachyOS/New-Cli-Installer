#include "cachyos/bootloader.hpp"

#include <expected>     // for unexpected
#include <string>       // for string
#include <string_view>  // for string_view

#include <spdlog/spdlog.h>

namespace cachyos::installer {

auto install_bootloader(const InstallContext& /*ctx*/,
    const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto install_grub_uefi(const InstallContext& /*ctx*/, std::string_view /*bootid*/,
    bool /*as_default*/, const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto install_grub_bios(const InstallContext& /*ctx*/,
    const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto install_refind(const InstallContext& /*ctx*/,
    const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto install_systemd_boot(const InstallContext& /*ctx*/,
    const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto install_limine(const InstallContext& /*ctx*/,
    const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto get_kernel_params(const InstallContext& /*ctx*/) noexcept
    -> std::expected<std::vector<std::string>, std::string> {
    return std::unexpected("not yet implemented");
}

auto grub_mkconfig(std::string_view /*mountpoint*/,
    const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

void configure_grub_common(gucc::bootloader::GrubConfig& /*grub_config*/,
    gucc::bootloader::GrubInstallConfig& /*grub_install_config*/,
    std::string_view /*mountpoint*/,
    std::string_view /*luks_dev*/,
    std::string_view /*zfs_extra_pkgs*/,
    std::string_view /*non_zfs_extra_pkgs*/) noexcept {
}

auto arch_chroot(std::string_view /*command*/, std::string_view /*mountpoint*/,
    const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

}  // namespace cachyos::installer
