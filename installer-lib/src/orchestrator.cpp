#include "cachyos/orchestrator.hpp"
#include "cachyos/disk.hpp"
#include "cachyos/steps.hpp"

// import gucc
#include "gucc/string_utils.hpp"

#include <cstdint>  // for uint8_t, uint32_t

#include <array>        // for array
#include <expected>     // for expected, unexpected
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for move

#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

// NOLINTNEXTLINE
using namespace cachyos::installer;

class SinkClearGuard {
 public:
    explicit SinkClearGuard(gucc::utils::ProcessRunner& runner) noexcept : m_runner(runner) { }
    ~SinkClearGuard() { m_runner.set_line_sink(nullptr); }

    SinkClearGuard(const SinkClearGuard&)                    = delete;
    SinkClearGuard(SinkClearGuard&&)                         = delete;
    auto operator=(const SinkClearGuard&) -> SinkClearGuard& = delete;
    auto operator=(SinkClearGuard&&) -> SinkClearGuard&      = delete;

 private:
    gucc::utils::ProcessRunner& m_runner;
};

enum class Step : std::uint8_t {
    Umount,
    Partition,
    Base,
    Fstab,
    EncryptSwap,
    SystemSettings,
    Users,
    MachineId,
    Desktop,
    DesktopConfigure,
    ServerPackages,
    SshKeys,
    ServerFirewall,
    Autologin,
    Chwd,
    NetworkCarryover,
    Bootloader,
    DetectCrypto,
    EnableServices,
    FinalValidation,
    BtrfsSnapshot,
    Cleanup,
    Count,
};

constexpr auto kTotalSteps = static_cast<std::int32_t>(Step::Count);

constexpr std::array<std::string_view, kTotalSteps> kStepMessages = {
    "Unmounting existing partitions..."sv,
    "Partitioning and mounting..."sv,
    "Installing base system (this may take a while)..."sv,
    "Generating fstab..."sv,
    "Configuring encrypted swap..."sv,
    "Configuring system settings..."sv,
    "Creating user accounts..."sv,
    "Generating machine ID..."sv,
    "Installing desktop environment..."sv,
    "Configuring desktop environment..."sv,
    "Installing server profile packages..."sv,
    "Installing SSH keys..."sv,
    "Configuring server firewall..."sv,
    "Configuring autologin..."sv,
    "Installing hardware-driver profiles..."sv,
    "Carrying network connections forward..."sv,
    "Installing bootloader..."sv,
    "Detecting encryption state..."sv,
    "Enabling system services..."sv,
    "Running final validation..."sv,
    "Creating installation snapshot..."sv,
    "Cleaning up..."sv,
};

constexpr auto step_index(Step s) noexcept {
    return static_cast<std::int32_t>(s);
}

constexpr auto step_message(Step s) noexcept -> std::string_view {
    return kStepMessages[static_cast<std::size_t>(s)];
}

auto emit_progress(const InstallSession& session,
    ProgressEventType type,
    std::int32_t step,
    std::string_view message) noexcept -> void {
    if (!session.on_progress) {
        return;
    }
    const auto fraction = static_cast<double>(step) / static_cast<double>(kTotalSteps);
    session.on_progress(ProgressEvent{
        .type     = type,
        .message  = std::string{message},
        .fraction = fraction,
    });
}

// fire a Failed event and hand back a ValidationResult with the formatted error
auto fail_step(const InstallSession& session,
    Step s,
    std::string_view label,
    std::string_view error,
    std::vector<std::string> prior_warnings) noexcept -> ValidationResult {
    emit_progress(session, ProgressEventType::Failed, step_index(s), label);
    return ValidationResult{
        .success  = false,
        .errors   = {fmt::format("{}: {}", label, error)},
        .warnings = std::move(prior_warnings),
    };
}

// fire a Cancelled event for the step we were about to run, hand back a result tagged cancelled
auto cancel_result(const InstallSession& session,
    Step s,
    std::vector<std::string> prior_warnings) noexcept -> ValidationResult {
    constexpr auto kCancelled = "Cancelled by user"sv;
    emit_progress(session, ProgressEventType::Cancelled, step_index(s), kCancelled);
    return ValidationResult{
        .success  = false,
        .errors   = {std::string{kCancelled}},
        .warnings = std::move(prior_warnings),
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
    const InstallSession& session) noexcept -> ValidationResult {
    using enum ProgressEventType;
    std::vector<std::string> warnings;

    // reset run state
    session.runner.reset_cancel();

    // parse pacman progress into session context
    Step current_step{Step::Umount};
    std::string current_msg;
    session.runner.set_line_sink([&session, &current_step, &current_msg](std::string_view line) {
        if (!session.on_progress) {
            return;
        }
        const auto frac = parse_pacman_progress(line);
        if (!frac) {
            return;
        }
        constexpr auto total = static_cast<double>(kTotalSteps);
        const double base    = static_cast<double>(step_index(current_step)) / total;
        session.on_progress(ProgressEvent{
            .type     = ProgressEventType::Running,
            .message  = current_msg,
            .fraction = base + (*frac / total),
        });
    });
    const SinkClearGuard sink_guard{session.runner};

    const auto begin_step = [&session, &current_step, &current_msg](Step step_obj) {
        current_step = step_obj;
        current_msg  = std::string{step_message(step_obj)};
        emit_progress(session, ProgressEventType::Running, step_index(step_obj), step_message(step_obj));
    };

    spdlog::info("Install orchestrator starting...");
    emit_progress(session, Started, 0, "Starting installation..."sv);

    // Unmount any existing partitions on the target.
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::Umount, std::move(warnings));
    }
    begin_step(Step::Umount);

    // TODO(vnepogodin): generally we don't want to support that,
    // so let it be for the sake of feature parity for now
    if (steps::needs_umount(ctx)) {
        if (auto res = steps::umount(ctx); !res) {
            spdlog::warn("umount_partitions: {}", res.error());
            warnings.emplace_back(fmt::format("Pre-install unmount: {}", res.error()));
        }
    }

    // Prepare the target disk using partition schema.
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::Partition, std::move(warnings));
    }
    begin_step(Step::Partition);
    if (auto res = steps::partition(ctx); !res) {
        return fail_step(session, Step::Partition, "Partitioning failed"sv, res.error(), std::move(warnings));
    }

    // Base system.
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::Base, std::move(warnings));
    }
    begin_step(Step::Base);
    if (auto res = steps::base(ctx); !res) {
        if (session.runner.cancelled()) {
            return cancel_result(session, Step::Base, std::move(warnings));
        }
        return fail_step(session, Step::Base, "Base install failed"sv, res.error(), std::move(warnings));
    }

    // Generate fstab.
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::Fstab, std::move(warnings));
    }
    begin_step(Step::Fstab);
    if (auto res = steps::fstab(ctx); !res) {
        return fail_step(session, Step::Fstab, "fstab generation failed"sv, res.error(), std::move(warnings));
    }

    // Optional LUKS swap.
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::EncryptSwap, std::move(warnings));
    }
    begin_step(Step::EncryptSwap);
    std::ranges::move(steps::encrypt_swap(ctx), std::back_inserter(warnings));

    // NOTE(vnepogodin): generally OEM setup should be after all installs, partitions etc

    // Apply system settings (hostname, locale, keymap, timezone, hw_clock).
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::SystemSettings, std::move(warnings));
    }
    begin_step(Step::SystemSettings);
    if (auto res = steps::system_settings(sys, ctx); !res) {
        return fail_step(session, Step::SystemSettings, "System settings failed"sv, res.error(), std::move(warnings));
    }

    // Root password + user account.
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::Users, std::move(warnings));
    }
    begin_step(Step::Users);
    std::ranges::move(
        steps::users(user, root_password, ctx),
        std::back_inserter(warnings));

    // Replace the live-ISO machine-id with a fresh one for the target.
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::MachineId, std::move(warnings));
    }
    begin_step(Step::MachineId);
    if (auto res = steps::machine_id(ctx); !res) {
        warnings.emplace_back(res.error());
    }

    // Desktop pacstrap (skipped in server mode).
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::Desktop, std::move(warnings));
    }
    begin_step(Step::Desktop);
    if (auto res = steps::desktop(ctx); !res) {
        warnings.emplace_back(res.error());
    }

    // Desktop post-install config (plymouth + service enable).
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::DesktopConfigure, std::move(warnings));
    }
    begin_step(Step::DesktopConfigure);
    if (auto res = steps::desktop_configure(ctx); !res) {
        warnings.emplace_back(res.error());
    }

    // server edition related
    if (ctx.resolved_server) {
        if (session.runner.cancelled()) {
            return cancel_result(session, Step::ServerPackages, std::move(warnings));
        }
        begin_step(Step::ServerPackages);
        if (auto res = steps::server_packages(ctx); !res) {
            warnings.emplace_back(res.error());
        }

        if (session.runner.cancelled()) {
            return cancel_result(session, Step::SshKeys, std::move(warnings));
        }
        begin_step(Step::SshKeys);
        if (auto res = steps::ssh_keys(user, ctx); !res) {
            warnings.emplace_back(res.error());
        }

        if (session.runner.cancelled()) {
            return cancel_result(session, Step::ServerFirewall, std::move(warnings));
        }
        begin_step(Step::ServerFirewall);
        if (auto res = steps::server_firewall(ctx); !res) {
            warnings.emplace_back(res.error());
        }
    }

    // Autologin.
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::Autologin, std::move(warnings));
    }
    begin_step(Step::Autologin);
    if (auto res = steps::autologin(user, ctx); !res) {
        warnings.emplace_back(res.error());
    }

    // chwd hardware-driver profiles (opt-in).
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::Chwd, std::move(warnings));
    }
    begin_step(Step::Chwd);
    if (auto res = steps::chwd(ctx); !res) {
        warnings.emplace_back(res.error());
    }

    // Carry the live ISO's NetworkManager connection profiles into the target.
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::NetworkCarryover, std::move(warnings));
    }
    begin_step(Step::NetworkCarryover);
    if (ctx.carry_live_network && steps::network_carryover(ctx) < 0) {
        warnings.emplace_back("network connection carryover failed");
    }

    // Bootloader.
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::Bootloader, std::move(warnings));
    }
    begin_step(Step::Bootloader);
    if (auto res = steps::bootloader(ctx); !res) {
        warnings.emplace_back(res.error());
    }

    // Detect post-install crypto state and stash it on the context for kernel-params use.
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::DetectCrypto, std::move(warnings));
    }
    begin_step(Step::DetectCrypto);

    [[maybe_unused]] const auto crypto_res = steps::detect_crypto(ctx);

    // Enable systemd services.
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::EnableServices, std::move(warnings));
    }
    begin_step(Step::EnableServices);
    if (auto res = steps::enable_services(ctx); !res) {
        warnings.emplace_back(res.error());
    }

    // Final validation.
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::FinalValidation, std::move(warnings));
    }
    begin_step(Step::FinalValidation);
    {
        auto check = steps::final_validation(ctx);
        for (auto& err : check.errors) {
            warnings.emplace_back(fmt::format("final_check: {}", std::move(err)));
        }
        for (auto& warn : check.warnings) {
            warnings.emplace_back(fmt::format("final_check: {}", std::move(warn)));
        }
    }

    // Create a permanent btrfs installation snapshot
    if (session.runner.cancelled()) {
        return cancel_result(session, Step::BtrfsSnapshot, std::move(warnings));
    }
    begin_step(Step::BtrfsSnapshot);
    if (auto res = steps::btrfs_snapshot(ctx); !res) {
        warnings.emplace_back(res.error());
    }

    // Copy install log into target and unmount.
    begin_step(Step::Cleanup);
    std::ranges::move(steps::cleanup(ctx), std::back_inserter(warnings));

    emit_progress(session, Completed, kTotalSteps, "Installation complete!"sv);
    spdlog::info("Install orchestrator finished.");

    return ValidationResult{
        .success  = true,
        .errors   = {},
        .warnings = std::move(warnings),
    };
}

}  // namespace cachyos::installer
