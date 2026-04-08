#ifndef CACHYOS_INSTALLER_DATA_HPP
#define CACHYOS_INSTALLER_DATA_HPP

#include "gucc/bootloader.hpp"

#include <array>        // for array
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace cachyos::installer::data {

using namespace std::string_view_literals;

inline constexpr std::array available_kernels{
    "linux-cachyos"sv, "linux-cachyos-server"sv, "linux-cachyos-eevdf"sv, "linux-cachyos-bore"sv,
    "linux-cachyos-hardened"sv, "linux-cachyos-lts"sv, "linux-cachyos-rc"sv, "linux-cachyos-rt-bore"sv};

inline constexpr std::array available_desktops{
    "kde"sv,       // KDE Plasma
    "gnome"sv,     // Gnome
    "xfce"sv,      // Xfce4
    "bspwm"sv,     // BSPWM (empty)
    "budgie"sv,    // Budgie
    "cinnamon"sv,  // Cinnamon
    "cosmic"sv,    // Cosmic
    "i3wm"sv,      // i3 Window Manager
    "hyprland"sv,  // Hyprland
    "niri"sv,      // Niri
    "lxde"sv,      // LXDE
    "lxqt"sv,      // LXQT
    "mate"sv,      // Mate
    "openbox"sv,   // Openbox
    "qtile"sv,     // Qtile
    "sway"sv,      // Sway
    "ukui"sv,      // UKUI
    "wayfire"sv,   // Wayfire
};

inline constexpr std::array available_filesystems{"ext4"sv, "btrfs"sv, "xfs"sv};

inline constexpr std::array available_shells{"/bin/bash"sv, "/bin/zsh"sv, "/usr/bin/fish"sv};

// helper for vector
template <std::size_t N>
inline auto to_vec(const std::array<std::string_view, N>& arr) -> std::vector<std::string> {
    return {arr.begin(), arr.end()};
}

// Returns available bootloaders for the given system mode.
constexpr auto available_bootloaders(std::string_view bios_mode) noexcept -> std::vector<gucc::bootloader::BootloaderType> {
    using gucc::bootloader::BootloaderType;

    if (bios_mode == "BIOS"sv) {
        return {BootloaderType::Grub};
    }
    return {BootloaderType::SystemdBoot, BootloaderType::Refind, BootloaderType::Grub, BootloaderType::Limine};
}

// Get list of disk devices as display strings, e.g. "/dev/sda 500.0GiB"
auto get_device_list() noexcept -> std::vector<std::string>;

// Parse device path from a "device size" entry (e.g. "/dev/sda 500.0GiB" -> "/dev/sda")
auto parse_device_name(std::string_view entry) noexcept -> std::string;

}  // namespace cachyos::installer::data

#endif  // CACHYOS_INSTALLER_DATA_HPP
