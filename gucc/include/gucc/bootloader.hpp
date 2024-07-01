#ifndef BOOTLOADER_HPP
#define BOOTLOADER_HPP

#include <string_view>  // for string_view

namespace gucc::bootloader {

// Installs & configures systemd-boot on system
auto install_systemd_boot(std::string_view root_mountpoint, std::string_view efi_directory, bool is_volume_removable) noexcept -> bool;

}  // namespace gucc::bootloader

#endif  // BOOTLOADER_HPP
