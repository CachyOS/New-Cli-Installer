#include "cachyos/bootloader.hpp"
#include "cachyos/disk.hpp"
#include "cachyos/packages.hpp"
#include "cachyos/result.hpp"
#include "cachyos/system.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/conf.hpp"
#include "gucc/crypto_detection.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/kernel_params.hpp"
#include "gucc/mount_partitions.hpp"
#include "gucc/mtab.hpp"
#include "gucc/pacman.hpp"
#include "gucc/string_utils.hpp"

#include <expected>     // for unexpected
#include <filesystem>   // for exists, create_directory
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for move
#include <vector>       // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;
namespace fs = std::filesystem;

namespace {

using cachyos::installer::InstallContext;
using cachyos::installer::lift;
using gucc::utils::target_path;

struct TargetRoot final {
    std::vector<gucc::disk::BlockDevice> devices;
    // lsblk has no entry for zfs datasets.
    std::optional<gucc::disk::BlockDevice> device;
    gucc::mtab::MTabEntry mount;
    bool is_removable{};

    [[nodiscard]] auto is_btrfs_subvol() const noexcept -> bool {
        return mount.fstype == "btrfs"sv && gucc::mtab::mount_option_value(mount.options, "subvolid"sv) != "5"sv;
    }
};

auto probe_target_root(std::string_view mountpoint) noexcept -> std::expected<TargetRoot, std::string> {
    auto devices = cachyos::installer::block_devices();
    if (!devices) {
        return std::unexpected(std::move(devices.error()));
    }
    auto mount = cachyos::installer::find_mount_at(mountpoint);
    if (!mount) {
        return std::unexpected(fmt::format(FMT_COMPILE("nothing is mounted at {}"), mountpoint));
    }
    auto device             = gucc::disk::find_device_by_mountpoint(*devices, mountpoint);
    const bool is_removable = cachyos::installer::is_volume_removable(*devices, mountpoint);
    return TargetRoot{
        .devices      = *std::move(devices),
        .device       = std::move(device),
        .mount        = *std::move(mount),
        .is_removable = is_removable,
    };
}

class HostLvmBind final {
 public:
    HostLvmBind() noexcept {
        std::error_code ec{};
        fs::create_directory(kPath, ec);
        auto bind = gucc::mount::ScopedMount::bind("/run/lvm"sv, kPath);
        if (!bind) {
            spdlog::warn("failed to bind-mount /run/lvm at {}: {}", kPath, bind.error().context);
            return;
        }
        m_bind.emplace(*std::move(bind));
    }
    ~HostLvmBind() {
        m_bind.reset();
        std::error_code ec{};
        fs::remove(kPath, ec);
    }
    HostLvmBind(const HostLvmBind&)                    = delete;
    HostLvmBind(HostLvmBind&&)                         = delete;
    auto operator=(const HostLvmBind&) -> HostLvmBind& = delete;
    auto operator=(HostLvmBind&&) -> HostLvmBind&      = delete;

 private:
    // TODO(vnepogodin): shouldn't be hardcoded mountpoint
    static constexpr auto kPath = "/mnt/hostlvm"sv;
    std::optional<gucc::mount::ScopedMount> m_bind;
};

auto grub_installer_path(std::string_view mountpoint) noexcept -> std::string {
    return target_path(mountpoint, "/usr/bin/grub_installer.sh"sv);
}

// TODO(vnepogodin): plymouth related should probably be also removed/moved fully to gucc, or even out of installer at all
auto build_default_cmdline(std::string_view mountpoint) noexcept -> std::string {
    auto cmdline = fmt::format(FMT_COMPILE("{}"), fmt::join(gucc::fs::kDefaultKernelParams, " "));
    if (std::error_code ec{}; fs::exists(target_path(mountpoint, "/usr/bin/plymouth"sv), ec)) {
        cmdline = fmt::format(FMT_COMPILE("splash {}"), cmdline);
    }
    return cmdline;
}

auto configure_grub_common(gucc::bootloader::GrubConfig& grub_config,
    gucc::bootloader::GrubInstallConfig& grub_install_config,
    const InstallContext& ctx, const TargetRoot& root, std::string_view base_pkgs) noexcept
    -> std::expected<void, std::string> {
    const auto& mountpoint = ctx.mountpoint;
    const bool is_zfs      = root.mount.fstype == "zfs"sv;
    auto packages          = std::string{base_pkgs};

    if (is_zfs) {
        std::error_code ec{};

        // create or overwrite env for zfs
        const auto env_path = target_path(mountpoint, "/etc/environment"sv);
        const auto env      = fs::exists(env_path, ec) ? gucc::file_utils::read_whole_file(env_path) : std::string{};
        if (auto res = gucc::file_utils::write_file(env_path, gucc::conf::set_directive(env, "ZPOOL_VDEV_NAME_PATH"sv, "ZPOOL_VDEV_NAME_PATH=YES"sv, gucc::conf::KeyStyle::Assign)); !res) {
            spdlog::warn("{}", gucc::to_string(res.error()));
        }

        grub_install_config.is_root_on_zfs = true;
        grub_config.savedefault            = std::nullopt;

        const auto& zroot_var             = fmt::format(FMT_COMPILE("zfs={} rw"), root.mount.device);
        grub_config.cmdline_linux_default = fmt::format(FMT_COMPILE("{} {}"), grub_config.cmdline_linux_default, zroot_var);
        grub_config.cmdline_linux         = fmt::format(FMT_COMPILE("{} {}"), grub_config.cmdline_linux, zroot_var);
    } else {
        // disable SAVEDEFAULT if on LVM or BTRFS
        if ((root.device && root.device->type == "lvm"sv) || root.mount.fstype == "btrfs"sv) {
            grub_config.savedefault = std::nullopt;
        }
        // grub-btrfs only makes sense when booting a subvolume
        if (root.is_btrfs_subvol()) {
            packages += " grub-btrfs"sv;
        }
        packages += " grub-hook"sv;
    }

    const auto& installer_path = grub_installer_path(mountpoint);
    const auto& bash_code      = fmt::format(FMT_COMPILE("#!/bin/bash\nln -s /hostlvm /run/lvm\npacman -S --noconfirm --needed {}\n"), packages);
    if (auto res = lift(gucc::file_utils::write_file(installer_path, bash_code)); !res) {
        return res;
    }
    std::error_code ec{};
    fs::permissions(installer_path, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);

    if (const auto& luks_dev = ctx.crypto.luks_dev; !luks_dev.empty()) {
        grub_config.cmdline_linux = fmt::format(FMT_COMPILE("{} {}"), gucc::utils::first_field(luks_dev), grub_config.cmdline_linux);
        spdlog::info("adding kernel parameter {}", luks_dev);
    }

    // If Full disk encryption is used, use a keyfile
    if (ctx.crypto.is_fde) {
        spdlog::info("Full disk encryption enabled");
        grub_config.enable_cryptodisk = true;
    }
    return {};
}

auto install_grub_with(const InstallContext& ctx, const TargetRoot& root,
    gucc::bootloader::GrubConfig& grub_config, gucc::bootloader::GrubInstallConfig& grub_install_config,
    std::string_view base_pkgs) noexcept -> std::expected<void, std::string> {
    grub_config.cmdline_linux_default = build_default_cmdline(ctx.mountpoint);
    grub_install_config.do_recheck    = true;
    grub_install_config.is_removable  = root.is_removable;

    if (auto res = configure_grub_common(grub_config, grub_install_config, ctx, root, base_pkgs); !res) {
        return res;
    }
    {
        const HostLvmBind host_lvm{};
        if (auto res = cachyos::installer::arch_chroot("grub_installer.sh", ctx.mountpoint); !res) {
            spdlog::warn("grub_installer.sh: {}", res.error());
        }
    }
    std::error_code ec{};
    fs::remove(grub_installer_path(ctx.mountpoint), ec);

    return lift(gucc::bootloader::install_grub(grub_config, grub_install_config, ctx.mountpoint));
}

auto install_grub_uefi(const InstallContext& ctx, const TargetRoot& root, std::string_view bootid, bool as_default) noexcept
    -> std::expected<void, std::string> {
    spdlog::info("Boot ID: {}", bootid);
    spdlog::info("Set as default: {}", as_default);

    gucc::bootloader::GrubConfig grub_config{};
    gucc::bootloader::GrubInstallConfig grub_install_config{
        .is_efi        = true,
        .efi_directory = ctx.uefi_mount,
        .bootloader_id = std::string{bootid},
    };
    if (auto res = install_grub_with(ctx, root, grub_config, grub_install_config, "grub efibootmgr dosfstools"sv); !res) {
        return res;
    }

    /* clang-format off */
    if (!as_default) { return {}; }
    /* clang-format on */

    // copy cachyos efi as the fallback boot entry
    const auto& boot_mountpoint = target_path(ctx.mountpoint, ctx.uefi_mount);
    std::error_code ec{};
    fs::create_directories(target_path(boot_mountpoint, "/EFI/boot"sv), ec);

    const auto& efi_cachyos_grub_file = target_path(boot_mountpoint, "/EFI/cachyos/grubx64.efi"sv);
    spdlog::info("Grub efi binary status:(EFI/cachyos/grubx64.efi): {}", fs::exists(efi_cachyos_grub_file, ec));

    if (auto res = gucc::file_utils::copy_file(efi_cachyos_grub_file, target_path(boot_mountpoint, "/EFI/boot/bootx64.efi"sv)); !res) {
        spdlog::warn("Failed to copy default EFI boot file: {}", res.error().context);
    }
    return {};
}

auto install_grub_bios(const InstallContext& ctx, const TargetRoot& root) noexcept
    -> std::expected<void, std::string> {
    gucc::bootloader::GrubConfig grub_config{};
    gucc::bootloader::GrubInstallConfig grub_install_config{
        .is_efi = false,
        .device = ctx.device,
    };

    // if /boot is LVM (whether using a separate /boot mount or not), amend grub
    if ((ctx.crypto.is_lvm && ctx.crypto.lvm_sep_boot == 0) || ctx.crypto.lvm_sep_boot == 2) {
        grub_config.preload_modules = fmt::format(FMT_COMPILE("lvm {}"), grub_config.preload_modules);
    }
    if (ctx.crypto.is_lvm || ctx.crypto.lvm_sep_boot == 2) {
        grub_config.savedefault = std::nullopt;
    }
    grub_config.disable_os_prober = ctx.disable_os_prober;

    if (auto res = gucc::bootloader::clear_bios_boot(ctx.device); !res) {
        spdlog::warn("Failed to clear the BIOS boot of {}: {}", ctx.device, res.error().context);
    }
    return install_grub_with(ctx, root, grub_config, grub_install_config, "grub os-prober"sv);
}

auto kernel_params(const InstallContext& ctx, const TargetRoot& root) noexcept
    -> std::expected<std::vector<std::string>, std::string> {
    const auto& root_name = root.device ? std::string{gucc::disk::strip_device_prefix(root.device->name)} : std::string{};

    auto root_part = gucc::fs::Partition{
        .fstype     = root.mount.fstype,
        .mountpoint = "/"s,
        .device     = root.device ? root.device->name : root.mount.device,
    };
    if (root.device) {
        root_part.uuid_str = root.device->uuid;
    }
    if (root.is_btrfs_subvol()) {
        root_part.subvolume = std::string{gucc::mtab::mount_option_value(root.mount.options, "subvol"sv).value_or(""sv)};
        spdlog::debug("root btrfs subvol := '{}'", *root_part.subvolume);
    }

    const auto& crypto = gucc::disk::detect_crypto_for_mountpoint(root.devices, ctx.mountpoint);
    if (crypto && crypto->is_luks) {
        root_part.luks_mapper_name = crypto->luks_mapper_name;
        if (!crypto->luks_uuid.empty()) {
            root_part.luks_uuid = crypto->luks_uuid;
        }
        spdlog::debug("kernel_params: luks_mapper_name:='{}'", *root_part.luks_mapper_name);
    } else if (ctx.crypto.is_luks || (root.device && root.device->type == "lvm"sv)) {
        root_part.luks_mapper_name = root_name;
        spdlog::debug("kernel_params: luks_mapper_name:='{}'", *root_part.luks_mapper_name);
    }

    const auto zfs_root_dataset = (root.mount.fstype == "zfs"sv) ? std::make_optional(root.mount.device) : std::nullopt;
    auto result                 = gucc::fs::get_kernel_params({std::move(root_part)}, build_default_cmdline(ctx.mountpoint), zfs_root_dataset);
    if (!result) {
        return std::unexpected("failed to compute kernel parameters");
    }
    return *std::move(result);
}

auto install_refind(const InstallContext& ctx, const TargetRoot& root) noexcept
    -> std::expected<void, std::string> {
    spdlog::info("Installing refind...");
    const auto& mountpoint = ctx.mountpoint;

    if (auto res = lift(gucc::pacman::ensure_installed("refind"sv)); !res) {
        return res;
    }
    auto params = kernel_params(ctx, root);
    if (!params) {
        return std::unexpected(std::move(params.error()));
    }

    const std::vector<std::string> extra_kernel_versions{
        "linux-cachyos", "linux-cachyos-server", "linux-cachyos-eevdf", "linux-cachyos-bore",
        "linux-cachyos-hardened", "linux-cachyos-lts", "linux-cachyos-rc", "linux-cachyos-rt-bore"};

    const gucc::bootloader::RefindInstallConfig refind_install_config{
        .is_removable          = root.is_removable,
        .root_mountpoint       = mountpoint,
        .boot_mountpoint       = target_path(mountpoint, ctx.uefi_mount),
        .extra_kernel_versions = extra_kernel_versions,
        .kernel_params         = *std::move(params),
    };
    if (auto res = lift(gucc::bootloader::install_refind(refind_install_config)); !res) {
        return res;
    }

    spdlog::info("Created rEFInd config:");
    const auto& refind_conf_content = gucc::file_utils::read_whole_file(target_path(mountpoint, "/boot/refind_linux.conf"sv));
    spdlog::info("[DUMP_TO_LOG] :=\n{}", refind_conf_content);

    if (auto res = cachyos::installer::install_packages({"refind-theme-nord"}, mountpoint, ctx.hostcache); !res) {
        return res;
    }

    spdlog::info("Refind was succesfully installed");
    return {};
}

auto install_systemd_boot(const InstallContext& ctx, const TargetRoot& root) noexcept
    -> std::expected<void, std::string> {
    spdlog::info("Installing systemd-boot...");
    const auto& mountpoint = ctx.mountpoint;

    if (auto res = cachyos::installer::install_packages({"systemd-boot-manager"}, mountpoint, ctx.hostcache); !res) {
        return std::unexpected(fmt::format(FMT_COMPILE("failed to install systemd-boot-manager: {}"), res.error()));
    }

    const gucc::bootloader::SystemdBootInstallConfig sdboot_config{
        .is_removable    = root.is_removable,
        .root_mountpoint = mountpoint,
        .efi_directory   = ctx.uefi_mount,
    };
    if (auto res = lift(gucc::bootloader::install_systemd_boot(sdboot_config)); !res) {
        return res;
    }

    spdlog::info("Systemd-boot was installed");
    return {};
}

auto install_limine(const InstallContext& ctx, const TargetRoot& root) noexcept
    -> std::expected<void, std::string> {
    spdlog::info("Installing Limine...");
    const auto& mountpoint      = ctx.mountpoint;
    const auto& boot_mountpoint = target_path(mountpoint, ctx.uefi_mount);

    auto params = kernel_params(ctx, root);
    if (!params) {
        return std::unexpected(std::move(params.error()));
    }

    const bool is_efi_mode = ctx.is_uefi();
    const gucc::bootloader::LimineInstallConfig limine_install_config{
        .is_efi          = is_efi_mode,
        .is_removable    = root.is_removable,
        .root_mountpoint = mountpoint,
        .boot_mountpoint = boot_mountpoint,
        .kernel_params   = *std::move(params),
        .bios_device     = is_efi_mode ? std::nullopt : std::optional{ctx.device},
        .efi_directory   = ctx.uefi_mount,
    };

    if (auto res = cachyos::installer::install_packages({"limine-mkinitcpio-hook"}, mountpoint, ctx.hostcache); !res) {
        return std::unexpected(fmt::format(FMT_COMPILE("failed to install limine-mkinitcpio-hook: {}"), res.error()));
    }

    // NOTE: should also be fatal with config/kind of option
    if (auto res = lift(gucc::pacman::ensure_installed("cachyos-wallpapers"sv)); !res) {
        spdlog::warn("Failed to install cachyos-wallpapers: {}", res.error());
    }

    const auto limine_splash = target_path(boot_mountpoint, "/limine-splash.png"sv);
    if (auto res = gucc::file_utils::copy_file("/usr/share/wallpapers/cachyos-wallpapers/limine-splash.png", limine_splash); !res) {
        spdlog::warn("Failed to copy limine splash: {}", res.error().context);
    }

    if (auto res = lift(gucc::bootloader::install_limine(limine_install_config)); !res) {
        return res;
    }

    // btrfs subvoles boot snapper support
    if (root.mount.fstype == "btrfs"sv) {
        if (auto res = cachyos::installer::install_packages({"cachyos-snapper-support", "limine-snapper-sync"}, mountpoint, ctx.hostcache); !res) {
            return res;
        }
    }

    spdlog::info("Limine was installed");
    return {};
}

auto uefi_bootloader(const InstallContext& ctx, const TargetRoot& root) noexcept
    -> std::expected<void, std::string> {
    if (auto res = cachyos::installer::ensure_efivarfs(); !res) {
        spdlog::error("{}", res.error());
    }

    using gucc::bootloader::BootloaderType;
    switch (ctx.bootloader) {
    case BootloaderType::Grub:
        return install_grub_uefi(ctx, root, "cachyos"sv, true);
    case BootloaderType::Refind:
        return install_refind(ctx, root);
    case BootloaderType::SystemdBoot:
        return install_systemd_boot(ctx, root);
    case BootloaderType::Limine:
        return install_limine(ctx, root);
    }
    return std::unexpected("unknown UEFI bootloader");
}

auto bios_bootloader(const InstallContext& ctx, const TargetRoot& root) noexcept
    -> std::expected<void, std::string> {
    using gucc::bootloader::BootloaderType;

    const auto& bootloader_str = gucc::bootloader::bootloader_to_string(ctx.bootloader);
    spdlog::info("Installing bios bootloader '{}'...", bootloader_str);

    switch (ctx.bootloader) {
    case BootloaderType::Limine:
        return install_limine(ctx, root);
    case BootloaderType::Grub:
        return install_grub_bios(ctx, root);
    default:
        return std::unexpected(fmt::format(FMT_COMPILE("unsupported BIOS bootloader: {}"), bootloader_str));
    }
}

template <class Install>
auto with_target_root(const InstallContext& ctx, const Install& install) noexcept {
    return probe_target_root(ctx.mountpoint).and_then([&](const TargetRoot& root) { return install(ctx, root); });
}

}  // namespace

namespace cachyos::installer {

auto install_bootloader(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string> {
    return with_target_root(ctx, ctx.is_uefi() ? uefi_bootloader : bios_bootloader);
}

auto install_grub_uefi(const InstallContext& ctx, std::string_view bootid,
    bool as_default) noexcept
    -> std::expected<void, std::string> {
    return with_target_root(ctx, [&](const InstallContext& c, const TargetRoot& root) {
        return ::install_grub_uefi(c, root, bootid, as_default);
    });
}

auto install_grub_bios(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string> {
    return with_target_root(ctx, ::install_grub_bios);
}

auto install_refind(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string> {
    return with_target_root(ctx, ::install_refind);
}

auto install_systemd_boot(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string> {
    return with_target_root(ctx, ::install_systemd_boot);
}

auto install_limine(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string> {
    return with_target_root(ctx, ::install_limine);
}

auto get_kernel_params(const InstallContext& ctx) noexcept
    -> std::expected<std::vector<std::string>, std::string> {
    return with_target_root(ctx, kernel_params);
}

auto grub_mkconfig(std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    return arch_chroot("grub-mkconfig -o /boot/grub/grub.cfg", mountpoint);
}

auto arch_chroot(std::string_view command, std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    if (!gucc::utils::arch_chroot_checked(command, mountpoint)) {
        return std::unexpected(fmt::format("failed to run in arch-chroot: {}", command));
    }
    return {};
}

}  // namespace cachyos::installer
