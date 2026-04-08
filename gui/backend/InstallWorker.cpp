#include "InstallWorker.hpp"

#include "cachyos/bootloader.hpp"
#include "cachyos/config.hpp"
#include "cachyos/crypto.hpp"
#include "cachyos/disk.hpp"
#include "cachyos/packages.hpp"
#include "cachyos/validation.hpp"

#include "gucc/subprocess.hpp"

#include <filesystem>

#include <spdlog/spdlog.h>

InstallWorker::InstallWorker(QObject* parent)
  : QObject(parent) {
}

void InstallWorker::setContext(cachyos::installer::InstallContext ctx,
    cachyos::installer::SystemSettings sys_settings,
    cachyos::installer::UserSettings user_settings,
    std::string root_password) {
    m_ctx          = std::move(ctx);
    m_sysSettings  = std::move(sys_settings);
    m_userSettings = std::move(user_settings);
    m_rootPassword = std::move(root_password);
}

void InstallWorker::emitProgress(int step, int total, const char* message) {
    const double fraction = static_cast<double>(step) / static_cast<double>(total);
    emit progressChanged(fraction, QString::fromUtf8(message));
}

void InstallWorker::run() {
    using namespace cachyos::installer;
    constexpr int total_steps = 12;
    const auto mountpoint     = m_ctx.mountpoint;

    spdlog::info("Install worker starting...");

    // Step 1: Unmount existing partitions
    emitProgress(0, total_steps, "Unmounting existing partitions...");
    if (auto res = umount_partitions(mountpoint, m_ctx.zfs_zpool_names, m_ctx.swap_device); !res) {
        spdlog::warn("umount_partitions: {}", res.error());
    }

    // Step 2: Auto-partition and mount
    emitProgress(1, total_steps, "Partitioning and mounting...");
    {
        auto partitions = auto_partition(m_ctx.device,
            m_ctx.system_mode == InstallContext::SystemMode::UEFI ? "UEFI" : "BIOS",
            m_ctx.bootloader, {});
        if (!partitions) {
            emit finished(false, QString::fromStdString("Auto-partition failed: " + partitions.error()));
            return;
        }

        // Build MountSelections from auto_partition output
        MountSelections mounts{};
        for (const auto& part : *partitions) {
            if (part.mountpoint == "/") {
                mounts.root = {
                    .device           = part.device,
                    .fstype           = part.fstype,
                    .mkfs_command     = "",
                    .mount_opts       = part.mount_opts,
                    .format_requested = true,
                };
            } else if (part.mountpoint.starts_with("/boot")) {
                mounts.esp = {
                    .device           = part.device,
                    .mountpoint       = part.mountpoint,
                    .format_requested = true,
                };
            }
        }

        // Use default btrfs subvolumes for btrfs
        if (mounts.root.fstype == "btrfs") {
            mounts.btrfs_subvolumes = default_btrfs_subvolumes();
        }

        auto mount_res = apply_mount_selections(mounts, mountpoint);
        if (!mount_res) {
            emit finished(false, QString::fromStdString("Mount failed: " + mount_res.error()));
            return;
        }

        m_ctx.partition_schema = std::move(mount_res->partitions);
        m_ctx.swap_device      = std::move(mount_res->swap_device);
    }

    // Step 3: Generate fstab
    emitProgress(2, total_steps, "Generating fstab...");
    if (auto res = generate_fstab(mountpoint); !res) {
        emit finished(false, QString::fromStdString("fstab generation failed: " + res.error()));
        return;
    }

    // Step 4: Apply system settings
    emitProgress(3, total_steps, "Configuring system settings...");
    if (auto res = apply_system_settings(m_sysSettings, mountpoint); !res) {
        emit finished(false, QString::fromStdString("System settings failed: " + res.error()));
        return;
    }

    // Step 5: Create user and set root password
    emitProgress(4, total_steps, "Creating user accounts...");
    if (auto res = set_root_password(m_rootPassword, mountpoint); !res) {
        spdlog::error("set_root_password: {}", res.error());
    }

    {
        gucc::utils::SubProcess child;
        if (auto res = create_user(m_userSettings, mountpoint, m_ctx.hostcache, child); !res) {
            spdlog::error("create_user: {}", res.error());
        }
    }

    // Step 6: Install base packages
    emitProgress(5, total_steps, "Installing base system (this may take a while)...");
    {
        gucc::utils::SubProcess child;
        if (auto res = install_base(m_ctx, child); !res) {
            emit finished(false, QString::fromStdString("Base install failed: " + res.error()));
            return;
        }
    }

    // Step 7: Install desktop (if not server mode)
    emitProgress(6, total_steps, "Installing desktop environment...");
    if (!m_ctx.server_mode && !m_ctx.desktop.empty()) {
        gucc::utils::SubProcess child;
        if (auto res = install_desktop(m_ctx.desktop, m_ctx, child); !res) {
            spdlog::error("install_desktop: {}", res.error());
        }
    }

    // Step 8: Install bootloader
    emitProgress(7, total_steps, "Installing bootloader...");
    {
        gucc::utils::SubProcess child;
        if (auto res = install_bootloader(m_ctx, child); !res) {
            spdlog::error("install_bootloader: {}", res.error());
        }
    }

    // Step 9: Crypto detection
    emitProgress(8, total_steps, "Detecting encryption state...");
    // Detect crypto for kernel params
    if (auto res = detect_crypto_root(mountpoint); res) {
        m_ctx.crypto.is_luks   = res->is_luks;
        m_ctx.crypto.is_lvm    = res->is_lvm;
        m_ctx.crypto.luks_dev  = res->luks_dev;
        m_ctx.crypto.luks_name = res->luks_name;
        m_ctx.crypto.luks_uuid = res->luks_uuid;
    }

    // Step 10: Enable services
    emitProgress(9, total_steps, "Enabling system services...");
    if (auto res = enable_services(m_ctx); !res) {
        spdlog::error("enable_services: {}", res.error());
    }

    // Step 11: Final check
    emitProgress(10, total_steps, "Running final validation...");
    {
        const auto& result = final_check(m_ctx);
        if (!result.success) {
            for (const auto& err : result.errors) {
                spdlog::error("final_check: {}", err);
            }
        }
        for (const auto& warn : result.warnings) {
            spdlog::warn("final_check: {}", warn);
        }
    }

    // Step 12: Copy log and unmount
    emitProgress(11, total_steps, "Cleaning up...");
    namespace fs = std::filesystem;
    if (fs::exists("/tmp/cachyos-install.log")) {
        const auto dest = mountpoint + "/cachyos-install.log";
        std::error_code ec;
        fs::copy_file("/tmp/cachyos-install.log", dest, fs::copy_options::overwrite_existing, ec);
    }

    if (auto res = umount_partitions(mountpoint, m_ctx.zfs_zpool_names, m_ctx.swap_device); !res) {
        spdlog::warn("Final umount: {}", res.error());
    }

    emitProgress(total_steps, total_steps, "Installation complete!");
    emit finished(true, {});
}
