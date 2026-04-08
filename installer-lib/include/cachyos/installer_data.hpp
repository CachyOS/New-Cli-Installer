#ifndef CACHYOS_INSTALLER_DATA_HPP
#define CACHYOS_INSTALLER_DATA_HPP

#include "gucc/bootloader.hpp"

#include <array>        // for array
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

// FIXME(vnepogodin): all that should be configurable via regular config files for the installer
namespace cachyos::installer::data {

using namespace std::string_view_literals;

inline constexpr std::array available_kernels{
    "linux-cachyos"sv, "linux-cachyos-server"sv, "linux-cachyos-eevdf"sv, "linux-cachyos-bore"sv,
    "linux-cachyos-hardened"sv, "linux-cachyos-lts"sv, "linux-cachyos-rc"sv, "linux-cachyos-rt-bore"sv};

inline constexpr std::array available_desktops{
    "kde"sv,       // KDE Plasma
    "gnome"sv,     // Gnome
    "xfce"sv,      // Xfce4
    "budgie"sv,    // Budgie
    "cinnamon"sv,  // Cinnamon
    "cosmic"sv,    // Cosmic
    "i3wm"sv,      // i3 Window Manager
    "hyprland"sv,  // Hyprland
    "niri"sv,      // Niri
    "lxde"sv,      // LXDE
    "lxqt"sv,      // LXQT
    "mate"sv,      // Mate
    "qtile"sv,     // Qtile
    "sway"sv,      // Sway
    "mangowm"sv,   // MangoWM
    "wayfire"sv,   // Wayfire
};

inline constexpr std::array available_filesystems{"btrfs"sv, "xfs"sv, "ext4"sv, "f2fs"sv, "zfs"sv};

inline constexpr std::array available_shells{"/bin/bash"sv, "/bin/zsh"sv, "/usr/bin/fish"sv};

// see forbidden user names from calamares
inline constexpr std::array forbidden_logins{
    "adm"sv,
    "bin"sv,
    "daemon"sv,
    "dbus"sv,
    "ftp"sv,
    "games"sv,
    "halt"sv,
    "http"sv,
    "lp"sv,
    "mail"sv,
    "man"sv,
    "news"sv,
    "nobody"sv,
    "ntp"sv,
    "operator"sv,
    "polkitd"sv,
    "postfix"sv,
    "root"sv,
    "rtkit"sv,
    "shutdown"sv,
    "sync"sv,
    "sys"sv,
    "systemd-coredump"sv,
    "systemd-network"sv,
    "systemd-resolve"sv,
    "systemd-timesync"sv,
    "uucp"sv,
    "uuidd"sv,
};

inline constexpr std::array forbidden_hostnames{"localhost"sv};

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

// m;eta for the bootloaders
struct BootloaderOption {
    std::string_view key;
    std::string_view label;
    std::string_view description;
};

constexpr auto bootloader_options(std::string_view bios_mode) noexcept -> std::vector<BootloaderOption> {
    constexpr auto grub_desc        = "GRUB is the oldest of the available boot managers. It has a very large feature set, works on almost every machine, and remains the most widely used Linux boot manager. Tip: Choose GRUB if you need encrypted /boot, BIOS compatibility, or want Btrfs snapshots with a stable, mature boot manager."sv;
    constexpr auto refind_desc      = "A fork of rEFIt, rEFInd was primarily made to make it easier for MacOS users to multi-boot. However, rEFInd has evolved into being hardware agnostic, making it a great choice for multi-booting on any system. The main draw of rEFInd is its ability to scan all storage devices at boot and correspondingly display entries for each OS/Kernel found. Tip: Choose rEFInd if you want a polished graphical interface and automatic multi-boot detection on UEFI systems."sv;
    constexpr auto systemdboot_desc = "Part of the systemd family, systemd-boot was created to be as simple as possible. This simple yet efficient design ensures it is reliable and fast, but it comes at the cost of advanced features supported by other boot managers. Tip: Choose systemd-boot if you prefer the simplest setup and don't require snapshots or advanced features. It's also the most reliable fallback for MSI motherboards with UEFI issues."sv;
    constexpr auto limine_desc      = "Limine is a modern, advanced, and portable multiprotocol bootloader. It serves as the reference implementation for the Limine boot protocol and supports Linux as well as chainloading other loaders. Tip: Choose Limine if you want a modern bootloader with Btrfs snapshot integration out of the box, plus support for both BIOS and UEFI and Windows dual-boot (via limine-scan)."sv;

    if (bios_mode == "BIOS"sv) {
        return {
            {.key = "grub"sv, .label = "GRUB"sv, .description = grub_desc},
        };
    }
    return {
        {.key = "systemd-boot"sv, .label = "systemd-boot"sv, .description = systemdboot_desc},
        {.key = "refind"sv, .label = "rEFInd"sv, .description = refind_desc},
        {.key = "grub"sv, .label = "GRUB"sv, .description = grub_desc},
        {.key = "limine"sv, .label = "Limine"sv, .description = limine_desc},
    };
}

// Get list of disk devices as display strings, e.g. "/dev/sda 500.0GiB"
auto get_device_list() noexcept -> std::vector<std::string>;

// Parse device path from a "device size" entry (e.g. "/dev/sda 500.0GiB" -> "/dev/sda")
auto parse_device_name(std::string_view entry) noexcept -> std::string;

}  // namespace cachyos::installer::data

#endif  // CACHYOS_INSTALLER_DATA_HPP
