#include "cachyos/orchestrator.hpp"
#include "cachyos/bootloader.hpp"
#include "cachyos/config.hpp"
#include "cachyos/crypto.hpp"
#include "cachyos/disk.hpp"
#include "cachyos/packages.hpp"
#include "cachyos/validation.hpp"

// import gucc
#include "gucc/subprocess.hpp"

#include <filesystem>   // for copy_file, exists
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for move

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

using namespace std::string_view_literals;

namespace {

// NOLINTNEXTLINE
using namespace cachyos::installer;

constexpr int kTotalSteps = 12;

auto emit_progress(const ExecutionCallbacks& cb,
    ProgressEventType type,
    int step,
    std::string_view message) noexcept -> void {
    if (!cb.on_progress) {
        return;
    }
    const double fraction = static_cast<double>(step) / static_cast<double>(kTotalSteps);
    cb.on_progress(ProgressEvent{
        .type     = type,
        .message  = std::string{message},
        .fraction = fraction,
    });
}

auto mount_selections_from_auto(const std::vector<gucc::fs::Partition>& partitions) noexcept -> MountSelections {
    MountSelections mounts{};
    for (const auto& part : partitions) {
        if (part.mountpoint == "/"sv) {
            mounts.root = {
                .device           = part.device,
                .fstype           = part.fstype,
                .mkfs_command     = "",
                .mount_opts       = part.mount_opts,
                .format_requested = true,
            };
        } else if (part.mountpoint.starts_with("/boot"sv)) {
            mounts.esp = {
                .device           = part.device,
                .mountpoint       = part.mountpoint,
                .format_requested = true,
            };
        }
    }
    if (mounts.root.fstype == "btrfs"sv) {
        mounts.btrfs_subvolumes = default_btrfs_subvolumes();
    }
    return mounts;
}

auto make_failure(std::string message, std::vector<std::string> prior_warnings) noexcept -> ValidationResult {
    return ValidationResult{
        .success  = false,
        .errors   = {std::move(message)},
        .warnings = std::move(prior_warnings),
    };
}

}  // namespace

namespace cachyos::installer {

auto run(InstallContext& ctx,
    const SystemSettings& sys,
    const UserSettings& user,
    std::string_view root_password,
    const ExecutionCallbacks& callbacks) noexcept -> ValidationResult {
    using enum ProgressEventType;
    const auto mountpoint = ctx.mountpoint;
    std::vector<std::string> warnings;

    spdlog::info("Install orchestrator starting...");
    emit_progress(callbacks, Started, 0, "Starting installation...");

    // Step 1: Unmount any existing partitions on the target.
    emit_progress(callbacks, Running, 0, "Unmounting existing partitions...");
    if (auto res = umount_partitions(mountpoint, ctx.zfs_zpool_names, ctx.swap_device); !res) {
        spdlog::warn("umount_partitions (pre-install): {}", res.error());
        warnings.emplace_back(fmt::format("Pre-install unmount: {}", res.error()));
    }

    // Step 2: Auto-partition and mount.
    emit_progress(callbacks, Running, 1, "Partitioning and mounting...");
    {
        const auto& bios_mode = ctx.system_mode == InstallContext::SystemMode::UEFI ? "UEFI"sv : "BIOS"sv;
        auto partitions             = auto_partition(ctx.device, bios_mode, ctx.bootloader, callbacks);
        if (!partitions) {
            emit_progress(callbacks, Failed, 1, "Auto-partition failed");
            return make_failure(fmt::format("Auto-partition failed: {}", partitions.error()), std::move(warnings));
        }

        const auto mounts = mount_selections_from_auto(*partitions);
        auto mount_res    = apply_mount_selections(mounts, mountpoint);
        if (!mount_res) {
            emit_progress(callbacks, Failed, 1, "Mount failed");
            return make_failure(fmt::format("Mount failed: {}", mount_res.error()), std::move(warnings));
        }

        ctx.partition_schema = std::move(mount_res->partitions);
        ctx.swap_device      = std::move(mount_res->swap_device);
    }

    // Step 3: Generate fstab.
    emit_progress(callbacks, Running, 2, "Generating fstab...");
    if (auto res = generate_fstab(mountpoint); !res) {
        emit_progress(callbacks, Failed, 2, "fstab generation failed");
        return make_failure(fmt::format("fstab generation failed: {}", res.error()), std::move(warnings));
    }

    // Step 4: Apply system settings (hostname, locale, keymap, timezone, hw_clock).
    emit_progress(callbacks, Running, 3, "Configuring system settings...");
    if (auto res = apply_system_settings(sys, mountpoint); !res) {
        emit_progress(callbacks, Failed, 3, "System settings failed");
        return make_failure(fmt::format("System settings failed: {}", res.error()), std::move(warnings));
    }

    // Step 5: Root password + user account.
    emit_progress(callbacks, Running, 4, "Creating user accounts...");
    if (auto res = set_root_password(root_password, mountpoint); !res) {
        spdlog::error("set_root_password: {}", res.error());
        warnings.emplace_back(fmt::format("set_root_password: {}", res.error()));
    }
    {
        gucc::utils::SubProcess child;
        child.set_log_line_callback(callbacks.on_log_line);
        if (auto res = create_user(user, mountpoint, ctx.hostcache, child); !res) {
            spdlog::error("create_user: {}", res.error());
            warnings.emplace_back(fmt::format("create_user: {}", res.error()));
        }
    }

    // Step 6: Base system.
    emit_progress(callbacks, Running, 5, "Installing base system (this may take a while)...");
    {
        gucc::utils::SubProcess child;
        child.set_log_line_callback(callbacks.on_log_line);
        if (auto res = install_base(ctx, child); !res) {
            emit_progress(callbacks, Failed, 5, "Base install failed");
            return make_failure(fmt::format("Base install failed: {}", res.error()), std::move(warnings));
        }
    }

    // Step 7: Desktop (skipped in server mode).
    emit_progress(callbacks, Running, 6, "Installing desktop environment...");
    if (!ctx.server_mode && !ctx.desktop.empty()) {
        gucc::utils::SubProcess child;
        child.set_log_line_callback(callbacks.on_log_line);
        if (auto res = install_desktop(ctx.desktop, ctx, child); !res) {
            spdlog::error("install_desktop: {}", res.error());
            warnings.emplace_back(fmt::format("install_desktop: {}", res.error()));
        }
    }

    // Step 8: Bootloader.
    emit_progress(callbacks, Running, 7, "Installing bootloader...");
    {
        gucc::utils::SubProcess child;
        child.set_log_line_callback(callbacks.on_log_line);
        if (auto res = install_bootloader(ctx, child); !res) {
            spdlog::error("install_bootloader: {}", res.error());
            warnings.emplace_back(fmt::format("install_bootloader: {}", res.error()));
        }
    }

    // Step 9: Detect post-install crypto state and stash it on the context for kernel-params use.
    emit_progress(callbacks, Running, 8, "Detecting encryption state...");
    if (auto res = detect_crypto_root(mountpoint); res) {
        ctx.crypto.is_luks   = res->is_luks;
        ctx.crypto.is_lvm    = res->is_lvm;
        ctx.crypto.luks_dev  = res->luks_dev;
        ctx.crypto.luks_name = res->luks_name;
        ctx.crypto.luks_uuid = res->luks_uuid;
    } else {
        spdlog::warn("detect_crypto_root: {}", res.error());
    }

    // Step 10: Enable systemd services.
    emit_progress(callbacks, Running, 9, "Enabling system services...");
    if (auto res = enable_services(ctx); !res) {
        spdlog::error("enable_services: {}", res.error());
        warnings.emplace_back(fmt::format("enable_services: {}", res.error()));
    }

    // Step 11: Final validation.
    emit_progress(callbacks, Running, 10, "Running final validation...");
    {
        auto check = final_check(ctx);
        for (auto& err : check.errors) {
            spdlog::error("final_check: {}", err);
            warnings.emplace_back(fmt::format("final_check: {}", std::move(err)));
        }
        for (auto& warn : check.warnings) {
            spdlog::warn("final_check: {}", warn);
            warnings.emplace_back(fmt::format("final_check: {}", std::move(warn)));
        }
    }

    // Step 12: Copy install log into target and unmount.
    emit_progress(callbacks, Running, 11, "Cleaning up...");
    {
        constexpr auto kLogSource = "/tmp/cachyos-install.log"sv;
        if (fs::exists(kLogSource)) {
            const auto dest = mountpoint + "/cachyos-install.log";
            std::error_code ec;
            fs::copy_file(kLogSource, dest, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                spdlog::warn("copy install log: {}", ec.message());
            }
        }
    }

    if (auto res = umount_partitions(mountpoint, ctx.zfs_zpool_names, ctx.swap_device); !res) {
        spdlog::warn("Final umount: {}", res.error());
        warnings.emplace_back(fmt::format("Final umount: {}", res.error()));
    }

    emit_progress(callbacks, Completed, kTotalSteps, "Installation complete!");
    spdlog::info("Install orchestrator finished.");

    return ValidationResult{
        .success  = true,
        .errors   = {},
        .warnings = std::move(warnings),
    };
}

}  // namespace cachyos::installer
