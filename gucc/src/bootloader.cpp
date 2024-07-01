#include "gucc/bootloader.hpp"
#include "gucc/initcpio.hpp"
#include "gucc/io_utils.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::bootloader {

auto install_systemd_boot(std::string_view root_mountpoint, std::string_view efi_directory, bool is_volume_removable) noexcept -> bool {
    // Install systemd-boot onto EFI
    const auto& bootctl_cmd = fmt::format(FMT_COMPILE("bootctl --path={} install"), efi_directory);
    if (!utils::arch_chroot_checked(bootctl_cmd, root_mountpoint)) {
        spdlog::error("Failed to run bootctl on path {} with: {}", root_mountpoint, bootctl_cmd);
        return false;
    }

    // Generate systemd-boot configuration entries with our sdboot
    static constexpr auto sdboot_cmd = "sdboot-manage gen"sv;
    if (!utils::arch_chroot_checked(bootctl_cmd, root_mountpoint)) {
        spdlog::error("Failed to run sdboot-manage gen on mountpoint: {}", root_mountpoint);
        return false;
    }

    // if the volume is removable don't use autodetect
    if (is_volume_removable) {
        const auto& initcpio_filename = fmt::format(FMT_COMPILE("{}/etc/mkinitcpio.conf"), root_mountpoint);

        // Remove autodetect hook
        auto initcpio = detail::Initcpio{initcpio_filename};
        initcpio.remove_hook("autodetect");
        spdlog::info("\"Autodetect\" hook was removed");
    }
    return true;
}

}  // namespace gucc::bootloader
