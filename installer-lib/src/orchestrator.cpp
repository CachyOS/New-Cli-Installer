#include "cachyos/orchestrator.hpp"
#include "cachyos/bootloader.hpp"
#include "cachyos/config.hpp"
#include "cachyos/crypto.hpp"
#include "cachyos/disk.hpp"
#include "cachyos/packages.hpp"
#include "cachyos/validation.hpp"

// import gucc
#include "gucc/string_utils.hpp"
#include "gucc/subprocess.hpp"

#include <array>        // for array
#include <cstdint>      // for uint8_t, uint32_t
#include <filesystem>   // for copy_file, exists
#include <optional>     // for optional
#include <stop_token>   // for stop_token, stop_callback
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

enum class Step : std::uint8_t {
    Umount,
    Partition,
    Fstab,
    SystemSettings,
    Users,
    Base,
    Desktop,
    Bootloader,
    DetectCrypto,
    EnableServices,
    FinalValidation,
    Cleanup,
    Count,
};

constexpr std::int32_t kTotalSteps = static_cast<std::int32_t>(Step::Count);

constexpr std::array<std::string_view, kTotalSteps> kStepMessages = {
    "Unmounting existing partitions..."sv,
    "Partitioning and mounting..."sv,
    "Generating fstab..."sv,
    "Configuring system settings..."sv,
    "Creating user accounts..."sv,
    "Installing base system (this may take a while)..."sv,
    "Installing desktop environment..."sv,
    "Installing bootloader..."sv,
    "Detecting encryption state..."sv,
    "Enabling system services..."sv,
    "Running final validation..."sv,
    "Cleaning up..."sv,
};

constexpr auto step_index(Step s) noexcept {
    return static_cast<std::int32_t>(s);
}

constexpr auto step_message(Step s) noexcept -> std::string_view {
    return kStepMessages[static_cast<std::size_t>(s)];
}

auto emit_progress(const ExecutionCallbacks& cb,
    ProgressEventType type,
    std::int32_t step,
    std::string_view message) noexcept -> void {
    if (!cb.on_progress) {
        return;
    }
    const auto fraction = static_cast<double>(step) / static_cast<double>(kTotalSteps);
    cb.on_progress(ProgressEvent{
        .type     = type,
        .message  = std::string{message},
        .fraction = fraction,
    });
}

void emit_step_running(const ExecutionCallbacks& cb, Step s) noexcept {
    emit_progress(cb, ProgressEventType::Running, step_index(s), step_message(s));
}

/// Emit a Failed event and return a ValidationResult with the formatted error.
auto fail_step(const ExecutionCallbacks& cb,
    Step s,
    std::string_view label,
    std::string_view error,
    std::vector<std::string> prior_warnings) noexcept -> ValidationResult {
    emit_progress(cb, ProgressEventType::Failed, step_index(s), label);
    return ValidationResult{
        .success  = false,
        .errors   = {fmt::format("{}: {}", label, error)},
        .warnings = std::move(prior_warnings),
    };
}

/// Emit a Cancelled event for the step we were about to run and return a
/// ValidationResult tagged as cancelled.
auto cancel_result(const ExecutionCallbacks& cb,
    Step s,
    std::vector<std::string> prior_warnings) noexcept -> ValidationResult {
    constexpr auto kCancelled = "Cancelled by user"sv;
    emit_progress(cb, ProgressEventType::Cancelled, step_index(s), kCancelled);
    return ValidationResult{
        .success  = false,
        .errors   = {std::string{kCancelled}},
        .warnings = std::move(prior_warnings),
    };
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

/// Builds a log-line callback for a step that forwards lines to the user's
/// sink and, when a pacman progress line is recognised, emits an intra-step
/// Running event scaled into this step's slice of the overall progress bar.
auto step_log_callback(const ExecutionCallbacks& cb, Step s) noexcept
    -> gucc::utils::SubProcess::LogLineCallback {
    if (!cb.on_log_line && !cb.on_progress) {
        return {};
    }
    const auto idx = step_index(s);
    auto msg       = std::string{step_message(s)};
    return [&cb, idx, msg = std::move(msg)](std::string_view line) {
        if (cb.on_log_line) {
            cb.on_log_line(line);
        }
        if (!cb.on_progress) {
            return;
        }
        const auto frac = parse_pacman_progress(line);
        if (!frac) {
            return;
        }
        constexpr auto total    = static_cast<double>(kTotalSteps);
        const double base       = static_cast<double>(idx) / total;
        const double step_width = 1.0 / total;
        cb.on_progress(ProgressEvent{
            .type     = ProgressEventType::Running,
            .message  = msg,
            .fraction = base + (*frac * step_width),
        });
    };
}

}  // namespace

namespace cachyos::installer {

auto parse_pacman_progress(std::string_view line) noexcept -> std::optional<double> {
    const auto open = line.find('(');
    if (open == std::string_view::npos) {
        return std::nullopt;
    }
    const auto slash = line.find('/', open + 1);
    if (slash == std::string_view::npos) {
        return std::nullopt;
    }
    const auto close = line.find(')', slash + 1);
    if (close == std::string_view::npos) {
        return std::nullopt;
    }

    const auto num_str   = gucc::utils::trim(line.substr(open + 1, slash - open - 1));
    const auto denom_str = gucc::utils::trim(line.substr(slash + 1, close - slash - 1));

    const auto num   = gucc::utils::parse_uint<std::uint32_t>(num_str);
    const auto denom = gucc::utils::parse_uint<std::uint32_t>(denom_str);
    if (!num || !denom || *denom == 0 || *num > *denom) {
        return std::nullopt;
    }
    return static_cast<double>(*num) / static_cast<double>(*denom);
}

auto run(InstallContext& ctx,
    const SystemSettings& sys,
    const UserSettings& user,
    std::string_view root_password,
    const ExecutionCallbacks& callbacks,
    std::stop_token stop_token) noexcept -> ValidationResult {
    using enum ProgressEventType;
    const auto mountpoint = ctx.mountpoint;
    std::vector<std::string> warnings;

    spdlog::info("Install orchestrator starting...");
    emit_progress(callbacks, Started, 0, "Starting installation..."sv);

    // Step 1: Unmount any existing partitions on the target.
    if (stop_token.stop_requested()) {
        return cancel_result(callbacks, Step::Umount, std::move(warnings));
    }
    emit_step_running(callbacks, Step::Umount);
    if (auto res = umount_partitions(mountpoint, ctx.zfs_zpool_names, ctx.swap_device); !res) {
        spdlog::warn("umount_partitions (pre-install): {}", res.error());
        warnings.emplace_back(fmt::format("Pre-install unmount: {}", res.error()));
    }

    // Step 2: Auto-partition and mount.
    if (stop_token.stop_requested()) {
        return cancel_result(callbacks, Step::Partition, std::move(warnings));
    }
    emit_step_running(callbacks, Step::Partition);
    {
        const auto& bios_mode = ctx.system_mode == InstallContext::SystemMode::UEFI ? "UEFI"sv : "BIOS"sv;
        auto partitions       = auto_partition(ctx.device, bios_mode, ctx.bootloader, callbacks);
        if (!partitions) {
            return fail_step(callbacks, Step::Partition, "Auto-partition failed"sv, partitions.error(), std::move(warnings));
        }

        const auto mounts = mount_selections_from_auto(*partitions);
        auto mount_res    = apply_mount_selections(mounts, mountpoint);
        if (!mount_res) {
            return fail_step(callbacks, Step::Partition, "Mount failed"sv, mount_res.error(), std::move(warnings));
        }

        ctx.partition_schema = std::move(mount_res->partitions);
        ctx.swap_device      = std::move(mount_res->swap_device);
    }

    // Step 3: Generate fstab.
    if (stop_token.stop_requested()) {
        return cancel_result(callbacks, Step::Fstab, std::move(warnings));
    }
    emit_step_running(callbacks, Step::Fstab);
    if (auto res = generate_fstab(mountpoint); !res) {
        return fail_step(callbacks, Step::Fstab, "fstab generation failed"sv, res.error(), std::move(warnings));
    }

    // Step 4: Apply system settings (hostname, locale, keymap, timezone, hw_clock).
    if (stop_token.stop_requested()) {
        return cancel_result(callbacks, Step::SystemSettings, std::move(warnings));
    }
    emit_step_running(callbacks, Step::SystemSettings);
    if (auto res = apply_system_settings(sys, mountpoint); !res) {
        return fail_step(callbacks, Step::SystemSettings, "System settings failed"sv, res.error(), std::move(warnings));
    }

    // Step 5: Root password + user account.
    if (stop_token.stop_requested()) {
        return cancel_result(callbacks, Step::Users, std::move(warnings));
    }
    emit_step_running(callbacks, Step::Users);
    if (auto res = set_root_password(root_password, mountpoint); !res) {
        spdlog::error("set_root_password: {}", res.error());
        warnings.emplace_back(fmt::format("set_root_password: {}", res.error()));
    }
    {
        gucc::utils::SubProcess child;
        child.set_log_line_callback(step_log_callback(callbacks, Step::Users));
        const std::stop_callback on_cancel(stop_token, [&child] { child.terminate(); });
        if (auto res = create_user(user, mountpoint, ctx.hostcache, child); !res) {
            spdlog::error("create_user: {}", res.error());
            warnings.emplace_back(fmt::format("create_user: {}", res.error()));
        }
    }

    // Step 6: Base system.
    if (stop_token.stop_requested()) {
        return cancel_result(callbacks, Step::Base, std::move(warnings));
    }
    emit_step_running(callbacks, Step::Base);
    {
        gucc::utils::SubProcess child;
        child.set_log_line_callback(step_log_callback(callbacks, Step::Base));
        const std::stop_callback on_cancel(stop_token, [&child] { child.terminate(); });
        if (auto res = install_base(ctx, child); !res) {
            if (stop_token.stop_requested()) {
                return cancel_result(callbacks, Step::Base, std::move(warnings));
            }
            return fail_step(callbacks, Step::Base, "Base install failed"sv, res.error(), std::move(warnings));
        }
    }

    // Step 7: Desktop (skipped in server mode).
    if (stop_token.stop_requested()) {
        return cancel_result(callbacks, Step::Desktop, std::move(warnings));
    }
    emit_step_running(callbacks, Step::Desktop);
    if (!ctx.server_mode && !ctx.desktop.empty()) {
        gucc::utils::SubProcess child;
        child.set_log_line_callback(step_log_callback(callbacks, Step::Desktop));
        const std::stop_callback on_cancel(stop_token, [&child] { child.terminate(); });
        if (auto res = install_desktop(ctx.desktop, ctx, child); !res) {
            spdlog::error("install_desktop: {}", res.error());
            warnings.emplace_back(fmt::format("install_desktop: {}", res.error()));
        }
    }

    // Step 8: Bootloader.
    if (stop_token.stop_requested()) {
        return cancel_result(callbacks, Step::Bootloader, std::move(warnings));
    }
    emit_step_running(callbacks, Step::Bootloader);
    {
        gucc::utils::SubProcess child;
        child.set_log_line_callback(step_log_callback(callbacks, Step::Bootloader));
        const std::stop_callback on_cancel(stop_token, [&child] { child.terminate(); });
        if (auto res = install_bootloader(ctx, child); !res) {
            spdlog::error("install_bootloader: {}", res.error());
            warnings.emplace_back(fmt::format("install_bootloader: {}", res.error()));
        }
    }

    // Step 9: Detect post-install crypto state and stash it on the context for kernel-params use.
    if (stop_token.stop_requested()) {
        return cancel_result(callbacks, Step::DetectCrypto, std::move(warnings));
    }
    emit_step_running(callbacks, Step::DetectCrypto);
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
    if (stop_token.stop_requested()) {
        return cancel_result(callbacks, Step::EnableServices, std::move(warnings));
    }
    emit_step_running(callbacks, Step::EnableServices);
    if (auto res = enable_services(ctx); !res) {
        spdlog::error("enable_services: {}", res.error());
        warnings.emplace_back(fmt::format("enable_services: {}", res.error()));
    }

    // Step 11: Final validation.
    if (stop_token.stop_requested()) {
        return cancel_result(callbacks, Step::FinalValidation, std::move(warnings));
    }
    emit_step_running(callbacks, Step::FinalValidation);
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
    emit_step_running(callbacks, Step::Cleanup);
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

    emit_progress(callbacks, Completed, kTotalSteps, "Installation complete!"sv);
    spdlog::info("Install orchestrator finished.");

    return ValidationResult{
        .success  = true,
        .errors   = {},
        .warnings = std::move(warnings),
    };
}

}  // namespace cachyos::installer
