#include "cachyos/bootloader.hpp"
#include "cachyos/disk.hpp"
#include "cachyos/packages.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/crypto_detection.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/kernel_params.hpp"
#include "gucc/subprocess.hpp"

#include <sys/mount.h>  // for mount, umount

#include <cerrno>   // for errno
#include <cstring>  // for strerror

#include <expected>     // for unexpected
#include <filesystem>   // for exists, create_directory
#include <fstream>      // for ofstream
#include <string>       // for string
#include <string_view>  // for string_view

#include <fmt/compile.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;
namespace fs = std::filesystem;

namespace {

using cachyos::installer::InstallContext;

/// Check if root is on a btrfs subvolume (not top-level subvolid=5).
auto is_root_on_btrfs_subvol(std::string_view mountpoint) noexcept -> bool {
    const auto& check_cmd = fmt::format(FMT_COMPILE("mount | awk '$3 == \"{}\" {{print $0}}' | grep btrfs | grep -qv subvolid=5"), mountpoint);
    return gucc::utils::exec_checked(check_cmd);
}

/// Extract the btrfs subvolume name from mount options for the given mountpoint.
auto get_btrfs_subvol_name(std::string_view mountpoint) noexcept -> std::string {
    const auto& cmd = fmt::format(FMT_COMPILE("mount | awk '$3 == \"{}\" {{print $6}}' | sed 's/^.*subvol=/subvol=/' | sed -e 's/,.*$/,/p' | sed 's/)//g' | sed 's/subvol=//'"), mountpoint);
    return gucc::utils::exec(cmd);
}

/// Build the default GRUB/kernel cmdline, prepending "splash" if plymouth is installed.
auto build_default_cmdline(std::string_view mountpoint) noexcept -> std::string {
    auto cmdline             = fmt::format("{}", fmt::join(gucc::fs::kDefaultKernelParams, " "));
    const auto plymouth_path = fmt::format(FMT_COMPILE("{}/usr/bin/plymouth"), mountpoint);
    if (fs::exists(plymouth_path)) {
        cmdline = fmt::format(FMT_COMPILE("splash {}"), cmdline);
    }
    return cmdline;
}

/// Applies common GRUB configuration.
void configure_grub_common(gucc::bootloader::GrubConfig& grub_config,
    gucc::bootloader::GrubInstallConfig& grub_install_config,
    std::string_view mountpoint,
    std::string_view luks_dev,
    bool is_fde,
    std::string_view zfs_extra_pkgs,
    std::string_view non_zfs_extra_pkgs) noexcept {
    const auto& root_part_fs        = gucc::fs::utils::get_mountpoint_fs(mountpoint);
    const auto& grub_installer_path = fmt::format(FMT_COMPILE("{}/usr/bin/grub_installer.sh"), mountpoint);

    // grub config changes for zfs root
    if (root_part_fs == "zfs"sv) {
        gucc::utils::exec(fmt::format(FMT_COMPILE("echo 'ZPOOL_VDEV_NAME_PATH=YES' >> {}/etc/environment"), mountpoint));

        grub_install_config.is_root_on_zfs = true;
        grub_config.savedefault            = std::nullopt;

        const auto& mountpoint_source     = gucc::fs::utils::get_mountpoint_source(mountpoint);
        const auto& zroot_var             = fmt::format(FMT_COMPILE("zfs={} rw"), mountpoint_source);
        grub_config.cmdline_linux_default = fmt::format(FMT_COMPILE("{} {}"), grub_config.cmdline_linux_default, zroot_var);
        grub_config.cmdline_linux         = fmt::format(FMT_COMPILE("{} {}"), grub_config.cmdline_linux, zroot_var);

        const auto& bash_code = fmt::format(FMT_COMPILE("#!/bin/bash\nln -s /hostlvm /run/lvm\npacman -S --noconfirm --needed {}\n"), zfs_extra_pkgs);
        std::ofstream grub_installer{grub_installer_path};
        grub_installer << bash_code;
    } else {
        // disable SAVEDEFAULT if on LVM or BTRFS
        const auto& root_blk_devices = gucc::disk::list_block_devices();
        const auto& root_blk_dev     = root_blk_devices
            ? gucc::disk::find_device_by_mountpoint(*root_blk_devices, "/"sv)
            : std::nullopt;

        const auto is_root_lvm = root_blk_dev && root_blk_dev->type == "lvm"sv;
        if (is_root_lvm || (root_part_fs == "btrfs"sv)) {
            grub_config.savedefault = std::nullopt;
        }

        const auto& bash_code = fmt::format(FMT_COMPILE("#!/bin/bash\nln -s /hostlvm /run/lvm\npacman -S --noconfirm --needed {}\n"), non_zfs_extra_pkgs);
        std::ofstream grub_installer{grub_installer_path};
        grub_installer << bash_code;
    }

    fs::permissions(grub_installer_path,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add);

    // If root is on btrfs subvolume, keep grub-btrfs; otherwise strip it
    if (!is_root_on_btrfs_subvol(mountpoint)) {
        gucc::utils::exec(fmt::format(FMT_COMPILE("sed -e 's/ grub-btrfs//g' -i {}"), grub_installer_path));
    }

    // If encryption used amend grub
    if (!luks_dev.empty()) {
        // Extract first word of luks_dev
        const auto first_space    = luks_dev.find(' ');
        const auto luks_dev_first = (first_space != std::string_view::npos)
            ? std::string{luks_dev.substr(0, first_space)}
            : std::string{luks_dev};
        grub_config.cmdline_linux = fmt::format(FMT_COMPILE("{} {}"), luks_dev_first, grub_config.cmdline_linux);
        spdlog::info("adding kernel parameter {}", luks_dev);
    }

    // If Full disk encryption is used, use a keyfile
    if (is_fde) {
        spdlog::info("Full disk encryption enabled");
        grub_config.enable_cryptodisk = true;
    }
}

/// Internal UEFI bootloader dispatcher.
auto uefi_bootloader(const InstallContext& ctx, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    // Ensure efivarfs is mounted
    static constexpr auto efi_path     = "/sys/firmware/efi/";
    static constexpr auto efivars_path = "/sys/firmware/efi/efivars";
    if (fs::exists(efi_path)
        && fs::is_directory(efi_path)
        && gucc::fs::utils::get_mountpoint_source(efivars_path).empty()) {
        if (mount("efivarfs", efivars_path, "efivarfs", 0, "") != 0) {
            spdlog::error("Failed to mount efivarfs: {}", std::strerror(errno));
        }
    }

    using gucc::bootloader::BootloaderType;
    switch (ctx.bootloader) {
    case BootloaderType::Grub:
        return install_grub_uefi(ctx, "cachyos"sv, true, child);
    case BootloaderType::Refind:
        return install_refind(ctx, child);
    case BootloaderType::SystemdBoot:
        return install_systemd_boot(ctx, child);
    case BootloaderType::Limine:
        return install_limine(ctx, child);
    }
    return std::unexpected("unknown UEFI bootloader");
}

/// Internal BIOS bootloader dispatcher.
auto bios_bootloader(const InstallContext& ctx, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    using gucc::bootloader::BootloaderType;

    const auto& bootloader_str = gucc::bootloader::bootloader_to_string(ctx.bootloader);
    spdlog::info("Installing bios bootloader '{}'...", bootloader_str);

    if (ctx.bootloader == BootloaderType::Limine) {
        return install_limine(ctx, child);
    }
    if (ctx.bootloader != BootloaderType::Grub) {
        return std::unexpected(fmt::format("unsupported BIOS bootloader: {}", bootloader_str));
    }
    return install_grub_bios(ctx, child);
}

}  // namespace

namespace cachyos::installer {

auto install_bootloader(const InstallContext& ctx, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    if (ctx.system_mode == InstallContext::SystemMode::BIOS) {
        return bios_bootloader(ctx, child);
    }
    return uefi_bootloader(ctx, child);
}

auto install_grub_uefi(const InstallContext& ctx, std::string_view bootid,
    bool as_default, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    const auto& mountpoint = ctx.mountpoint;
    const auto& uefi_mount = ctx.uefi_mount;
    const auto& luks_dev   = ctx.crypto.luks_dev;

    std::error_code err{};
    fs::create_directory("/mnt/hostlvm", err);
    gucc::utils::exec("mount --bind /run/lvm /mnt/hostlvm");

    spdlog::info("Boot ID: {}", bootid);
    spdlog::info("Set as default: {}", as_default);

    gucc::bootloader::GrubConfig grub_config_struct{};
    gucc::bootloader::GrubInstallConfig grub_install_config_struct{};

    // Override GRUB cmdline_linux_default with shared kernel param defaults
    grub_config_struct.cmdline_linux_default = build_default_cmdline(mountpoint);

    grub_install_config_struct.is_efi        = true;
    grub_install_config_struct.do_recheck    = true;
    grub_install_config_struct.efi_directory = uefi_mount;
    grub_install_config_struct.bootloader_id = bootid;

    // if the device is removable append removable to the grub-install
    grub_install_config_struct.is_removable = is_volume_removable(mountpoint);

    // Configure shared GRUB settings
    configure_grub_common(grub_config_struct, grub_install_config_struct,
        mountpoint, luks_dev, ctx.crypto.is_fde,
        "grub efibootmgr dosfstools"sv,
        "grub efibootmgr dosfstools grub-btrfs grub-hook"sv);

    const auto& grub_installer_path = fmt::format(FMT_COMPILE("{}/usr/bin/grub_installer.sh"), mountpoint);

    // install grub
    auto chroot_result = arch_chroot("grub_installer.sh", mountpoint, child);
    if (!chroot_result) {
        spdlog::warn("grub_installer.sh: {}", chroot_result.error());
    }

    if (!gucc::bootloader::install_grub(grub_config_struct, grub_install_config_struct, mountpoint)) {
        umount("/mnt/hostlvm");
        fs::remove("/mnt/hostlvm", err);
        return std::unexpected("failed to install grub");
    }
    umount("/mnt/hostlvm");
    fs::remove("/mnt/hostlvm", err);

    // the grub_installer is no longer needed
    fs::remove(grub_installer_path, err);

    /* clang-format off */
    if (!as_default) { return {}; }
    /* clang-format on */

    // create efi directories
    const auto& boot_mountpoint = fmt::format(FMT_COMPILE("{}{}"), mountpoint, uefi_mount);
    fs::create_directories(fmt::format(FMT_COMPILE("{}/EFI/boot"), boot_mountpoint), err);

    const auto& efi_cachyos_grub_file = fmt::format(FMT_COMPILE("{}/EFI/cachyos/grubx64.efi"), boot_mountpoint);
    spdlog::info("Grub efi binary status:(EFI/cachyos/grubx64.efi): {}", fs::exists(efi_cachyos_grub_file));

    // copy cachyos efi as default efi
    const auto& default_efi_grub_file = fmt::format(FMT_COMPILE("{}/EFI/boot/bootx64.efi"), boot_mountpoint);
    fs::copy_file(efi_cachyos_grub_file, default_efi_grub_file, fs::copy_options::overwrite_existing, err);
    if (err) {
        spdlog::warn("Failed to copy default EFI boot file: {}", err.message());
    }
    return {};
}

auto install_grub_bios(const InstallContext& ctx, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    const auto& mountpoint = ctx.mountpoint;
    const auto& luks_dev   = ctx.crypto.luks_dev;
    const auto& device     = ctx.device;

    gucc::bootloader::GrubConfig grub_config_struct{};
    gucc::bootloader::GrubInstallConfig grub_install_config_struct{};

    // Override GRUB cmdline_linux_default with shared kernel param defaults
    grub_config_struct.cmdline_linux_default = build_default_cmdline(mountpoint);

    grub_install_config_struct.is_efi     = false;
    grub_install_config_struct.do_recheck = true;
    grub_install_config_struct.device     = device;

    // if /boot is LVM (whether using a separate /boot mount or not), amend grub
    if ((ctx.crypto.is_lvm && ctx.crypto.lvm_sep_boot == 0) || ctx.crypto.lvm_sep_boot == 2) {
        grub_config_struct.preload_modules = fmt::format(FMT_COMPILE("lvm {}"), grub_config_struct.preload_modules);
        grub_config_struct.savedefault     = std::nullopt;
    }

    // Same setting is needed for LVM
    if (ctx.crypto.is_lvm) {
        grub_config_struct.savedefault = std::nullopt;
    }

    // enable os-prober
    grub_config_struct.disable_os_prober = false;

    // Configure shared GRUB settings (ZFS, btrfs, LUKS, FDE, grub_installer.sh)
    configure_grub_common(grub_config_struct, grub_install_config_struct,
        mountpoint, luks_dev, ctx.crypto.is_fde,
        "grub os-prober"sv,
        "grub os-prober grub-btrfs grub-hook"sv);

    const auto& grub_installer_path = fmt::format(FMT_COMPILE("{}/usr/bin/grub_installer.sh"), mountpoint);
    std::error_code err{};

    gucc::utils::exec(fmt::format(FMT_COMPILE("dd if=/dev/zero of={} seek=1 count=2047"), device));
    fs::create_directory("/mnt/hostlvm", err);
    gucc::utils::exec("mount --bind /run/lvm /mnt/hostlvm");

    // install grub
    auto chroot_result = arch_chroot("grub_installer.sh", mountpoint, child);
    if (!chroot_result) {
        spdlog::warn("grub_installer.sh: {}", chroot_result.error());
    }

    // the grub_installer is no longer needed
    fs::remove(grub_installer_path, err);

    umount("/mnt/hostlvm");
    fs::remove("/mnt/hostlvm", err);

    if (!gucc::bootloader::install_grub(grub_config_struct, grub_install_config_struct, mountpoint)) {
        return std::unexpected("failed to install grub");
    }
    return {};
}

auto install_refind(const InstallContext& ctx, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    spdlog::info("Installing refind...");
    const auto& mountpoint      = ctx.mountpoint;
    const auto& uefi_mount      = ctx.uefi_mount;
    const auto& boot_mountpoint = fmt::format(FMT_COMPILE("{}{}"), mountpoint, uefi_mount);

    auto needed_result = install_needed("refind", {});
    if (!needed_result) {
        return std::unexpected(needed_result.error());
    }

    const std::vector<std::string> extra_kernel_versions{
        "linux-cachyos", "linux-cachyos-server", "linux-cachyos-eevdf", "linux-cachyos-bore",
        "linux-cachyos-hardened", "linux-cachyos-lts", "linux-cachyos-rc", "linux-cachyos-rt-bore"};

    auto kernel_params = get_kernel_params(ctx);
    if (!kernel_params) {
        return std::unexpected(fmt::format("failed to get kernel params: {}", kernel_params.error()));
    }

    const gucc::bootloader::RefindInstallConfig refind_install_config{
        .is_removable          = is_volume_removable(mountpoint),
        .root_mountpoint       = mountpoint,
        .boot_mountpoint       = boot_mountpoint,
        .extra_kernel_versions = extra_kernel_versions,
        .kernel_params         = *kernel_params,
    };

    if (!gucc::bootloader::install_refind(refind_install_config)) {
        return std::unexpected("failed to install refind");
    }

    spdlog::info("Created rEFInd config:");
    const auto& refind_conf_content = gucc::file_utils::read_whole_file(fmt::format(FMT_COMPILE("{}/boot/refind_linux.conf"), mountpoint));
    spdlog::info("[DUMP_TO_LOG] :=\n{}", refind_conf_content);

    auto pkg_result = install_packages({"refind-theme-nord"}, mountpoint, ctx.hostcache, child);
    if (!pkg_result) {
        spdlog::warn("Failed to install refind-theme-nord: {}", pkg_result.error());
    }

    spdlog::info("Refind was succesfully installed");
    return {};
}

auto install_systemd_boot(const InstallContext& ctx, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    spdlog::info("Installing systemd-boot...");
    const auto& mountpoint = ctx.mountpoint;
    const auto& uefi_mount = ctx.uefi_mount;

    // preinstall systemd-boot-manager
    auto pkg_result = install_packages({"systemd-boot-manager"}, mountpoint, ctx.hostcache, child);
    if (!pkg_result) {
        return std::unexpected(fmt::format("failed to install systemd-boot-manager: {}", pkg_result.error()));
    }

    const gucc::bootloader::SystemdBootInstallConfig sdboot_config{
        .is_removable    = is_volume_removable(mountpoint),
        .root_mountpoint = mountpoint,
        .efi_directory   = uefi_mount,
    };
    if (!gucc::bootloader::install_systemd_boot(sdboot_config)) {
        return std::unexpected("failed to install systemd-boot");
    }

    spdlog::info("Systemd-boot was installed");
    return {};
}

auto install_limine(const InstallContext& ctx, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    spdlog::info("Installing Limine...");
    const auto& mountpoint = ctx.mountpoint;
    const auto& uefi_mount = ctx.uefi_mount;

    const auto& boot_mountpoint = fmt::format(FMT_COMPILE("{}{}"), mountpoint, uefi_mount);

    auto kernel_params = get_kernel_params(ctx);
    if (!kernel_params) {
        return std::unexpected(fmt::format("failed to get kernel params: {}", kernel_params.error()));
    }

    const bool is_efi_mode = (ctx.system_mode == InstallContext::SystemMode::UEFI);

    gucc::bootloader::LimineInstallConfig limine_install_config{
        .is_efi          = is_efi_mode,
        .is_removable    = is_volume_removable(mountpoint),
        .root_mountpoint = mountpoint,
        .boot_mountpoint = boot_mountpoint,
        .kernel_params   = *kernel_params,
        .efi_directory   = std::string{uefi_mount},
    };

    // For BIOS mode, set the bios_device
    if (!is_efi_mode) {
        limine_install_config.bios_device = ctx.device;
    }

    // Preinstall limine-mkinitcpio-hook
    auto pkg_result = install_packages({"limine-mkinitcpio-hook"}, mountpoint, ctx.hostcache, child);
    if (!pkg_result) {
        return std::unexpected(fmt::format("failed to install limine-mkinitcpio-hook: {}", pkg_result.error()));
    }

    // Install splash screen
    auto splash_result = install_needed("cachyos-wallpapers", {});
    if (!splash_result) {
        spdlog::warn("Failed to install cachyos-wallpapers: {}", splash_result.error());
    }

    // Copy cachyos splash screen
    const auto& limine_splash_file = fmt::format(FMT_COMPILE("{}/limine-splash.png"), boot_mountpoint);
    std::error_code err{};
    fs::copy_file("/usr/share/wallpapers/cachyos-wallpapers/limine-splash.png", limine_splash_file, fs::copy_options::overwrite_existing, err);
    if (err) {
        spdlog::warn("Failed to copy limine splash: {}", err.message());
    }

    // Start limine install & configuration
    if (!gucc::bootloader::install_limine(limine_install_config)) {
        return std::unexpected("failed to install Limine");
    }

    // Integrate Snapper support for btrfs
    const auto& filesystem_type = gucc::fs::utils::get_mountpoint_fs(mountpoint);
    if (filesystem_type == "btrfs"sv) {
        auto snapper_result = install_packages({"cachyos-snapper-support", "limine-snapper-sync"}, mountpoint, ctx.hostcache, child);
        if (!snapper_result) {
            spdlog::warn("Failed to install snapper support: {}", snapper_result.error());
        }
    }

    spdlog::info("Limine was installed");
    return {};
}

auto get_kernel_params(const InstallContext& ctx) noexcept
    -> std::expected<std::vector<std::string>, std::string> {
    const auto& mountpoint = ctx.mountpoint;

    const auto& blk_devices = gucc::disk::list_block_devices();
    const auto& mnt_device  = blk_devices
        ? gucc::disk::find_device_by_mountpoint(*blk_devices, mountpoint)
        : std::nullopt;

    std::string root_name;
    if (mnt_device) {
        root_name = std::string{gucc::disk::strip_device_prefix(mnt_device->name)};
    }

    const auto& part_fs   = gucc::fs::utils::get_mountpoint_fs(mountpoint);
    auto root_part_struct = gucc::fs::Partition{
        .fstype     = part_fs,
        .mountpoint = "/"s,
        .device     = mnt_device
            ? mnt_device->name
            : fmt::format(FMT_COMPILE("/dev/{}"), root_name),
    };

    const auto& root_part_uuid = gucc::fs::utils::get_device_uuid(root_part_struct.device);
    root_part_struct.uuid_str  = root_part_uuid;

    // Set subvol field in case ROOT on btrfs subvolume
    if ((root_part_struct.fstype == "btrfs"sv) && is_root_on_btrfs_subvol(mountpoint)) {
        root_part_struct.subvolume = get_btrfs_subvol_name(mountpoint);
        spdlog::debug("root btrfs subvol := '{}'", *root_part_struct.subvolume);
    }

    // Use crypto detection to populate LUKS fields for kernel params
    const auto& crypto = blk_devices
        ? gucc::disk::detect_crypto_for_mountpoint(*blk_devices, mountpoint)
        : std::nullopt;
    if (crypto && crypto->is_luks) {
        root_part_struct.luks_mapper_name = crypto->luks_mapper_name;
        if (!crypto->luks_uuid.empty()) {
            root_part_struct.luks_uuid = crypto->luks_uuid;
        }
        spdlog::debug("kernel_params: luks_mapper_name:='{}'", *root_part_struct.luks_mapper_name);
    } else if (ctx.crypto.is_luks || (mnt_device && mnt_device->type == "lvm"sv)) {
        root_part_struct.luks_mapper_name = root_name;
        spdlog::debug("kernel_params: luks_mapper_name:='{}'", *root_part_struct.luks_mapper_name);
    }

    std::vector<gucc::fs::Partition> partitions{};
    partitions.emplace_back(std::move(root_part_struct));

    // Build default kernel params from shared constexpr defaults
    auto base_params = build_default_cmdline(mountpoint);

    auto result = gucc::fs::get_kernel_params(partitions, base_params);
    if (!result) {
        return std::unexpected("failed to compute kernel parameters");
    }
    return *std::move(result);
}

auto grub_mkconfig(std::string_view mountpoint, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    return arch_chroot("grub-mkconfig -o /boot/grub/grub.cfg", mountpoint, child);
}

auto arch_chroot(std::string_view command, std::string_view mountpoint,
    gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string> {
    if (!gucc::utils::arch_chroot_follow(command, mountpoint, child)) {
        return std::unexpected(fmt::format("failed to run in arch-chroot: {}", command));
    }
    return {};
}

}  // namespace cachyos::installer
