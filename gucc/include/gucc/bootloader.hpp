#ifndef BOOTLOADER_HPP
#define BOOTLOADER_HPP

#include <cinttypes>

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::bootloader {

struct GrubConfig final {
    // e.g GRUB_DEFAULT=0
    std::string default_entry{"0"};
    // e.g GRUB_TIMEOUT=5
    std::int32_t grub_timeout{5};
    // e.g GRUB_DISTRIBUTOR="Arch"
    std::string grub_distributor{"Arch"};
    // e.g GRUB_CMDLINE_LINUX_DEFAULT="loglevel=3 quiet"
    std::string cmdline_linux_default{"loglevel=3 quiet"};
    // e.g GRUB_CMDLINE_LINUX=""
    std::string cmdline_linux{};
    // e.g GRUB_PRELOAD_MODULES="part_gpt part_msdos"
    std::string preload_modules{"part_gpt part_msdos"};
    // e.g #GRUB_ENABLE_CRYPTODISK=y
    std::optional<bool> enable_cryptodisk{};
    // e.g GRUB_TIMEOUT_STYLE=menu
    std::string timeout_style{"menu"};
    // e.g GRUB_TERMINAL_INPUT=console
    std::string terminal_input{"console"};
    // e.g #GRUB_TERMINAL_OUTPUT=console
    std::optional<std::string> terminal_output{};
    // e.g GRUB_GFXMODE=auto
    std::string gfxmode{"auto"};
    // e.g GRUB_GFXPAYLOAD_LINUX=keep
    std::string gfxpayload_linux{"keep"};
    // e.g #GRUB_DISABLE_LINUX_UUID=true
    std::optional<bool> disable_linux_uuid{};
    // e.g GRUB_DISABLE_RECOVERY=true
    bool disable_recovery{true};
    // e.g #GRUB_COLOR_NORMAL="light-blue/black"
    std::optional<std::string> color_normal{};
    // e.g #GRUB_COLOR_HIGHLIGHT="light-cyan/blue"
    std::optional<std::string> color_highlight{};
    // e.g #GRUB_BACKGROUND="/path/to/wallpaper"
    std::optional<std::string> background{};
    // e.g #GRUB_THEME="/path/to/gfxtheme"
    std::optional<std::string> theme{};
    // e.g #GRUB_INIT_TUNE="480 440 1"
    std::optional<std::string> init_tune{};
    // e.g #GRUB_SAVEDEFAULT=true
    std::optional<bool> savedefault{};
    // e.g #GRUB_DISABLE_SUBMENU=y
    std::optional<bool> disable_submenu{};
    // e.g #GRUB_DISABLE_OS_PROBER=false
    std::optional<bool> disable_os_prober{};
};

struct GrubInstallConfig final {
    bool is_efi{};
    bool do_recheck{};
    bool is_removable{};
    bool is_root_on_zfs{};
    std::optional<std::string> efi_directory{};
    std::optional<std::string> bootloader_id{};
};

struct RefindInstallConfig final {
    bool is_removable{};
    std::string_view root_mountpoint;
    std::string_view boot_mountpoint;
    const std::vector<std::string>& extra_kernel_versions;
    const std::vector<std::string>& kernel_params;
};

// Generate grub config into string
auto gen_grub_config(const GrubConfig& grub_config) noexcept -> std::string;

// Writes grub config to file on the system
auto write_grub_config(const GrubConfig& grub_config, std::string_view root_mountpoint) noexcept -> bool;

// Installs and configures grub on system
auto install_grub(const GrubConfig& grub_config, const GrubInstallConfig& grub_install_config, std::string_view root_mountpoint) noexcept -> bool;

// Installs & configures systemd-boot on system
auto install_systemd_boot(std::string_view root_mountpoint, std::string_view efi_directory, bool is_volume_removable) noexcept -> bool;

// Generate refind config into system
auto gen_refind_config(const std::vector<std::string>& kernel_params) noexcept -> std::string;

// Edit refind config with provided extra kernel strings
auto refind_write_extra_kern_strings(std::string_view file_path, const std::vector<std::string>& extra_kernel_versions) noexcept -> bool;

// Installs & configures refind on system
auto install_refind(const RefindInstallConfig& refind_install_config) noexcept -> bool;

}  // namespace gucc::bootloader

#endif  // BOOTLOADER_HPP
