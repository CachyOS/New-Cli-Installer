#ifndef INSTALLER_DATA_HPP
#define INSTALLER_DATA_HPP

#include <array>        // for array
#include <string>       // for string
#include <string_view>  // for string_view_literals
#include <vector>       // for vector

namespace installer::data {

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

// Get list of disk devices as display strings, e.g. "/dev/sda 500.0GiB"
auto get_device_list() noexcept -> std::vector<std::string>;

// Parse device path from a "device size" entry (e.g. "/dev/sda 500.0GiB" -> "/dev/sda")
auto parse_device_name(std::string_view entry) noexcept -> std::string;

}  // namespace installer::data

#endif  // INSTALLER_DATA_HPP
