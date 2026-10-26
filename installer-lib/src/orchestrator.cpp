#include "cachyos/orchestrator.hpp"
#include "cachyos/steps.hpp"

// import gucc
#include "gucc/logger.hpp"
#include "gucc/string_utils.hpp"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint32_t

#include <algorithm>    // for transform
#include <expected>     // for expected
#include <iterator>     // for back_inserter
#include <optional>     // for optional
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for move
#include <variant>      // for get_if
#include <vector>       // for vector

#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

// NOLINTNEXTLINE
using namespace cachyos::installer;

constexpr auto fatal_label(StepId step) noexcept -> std::string_view {
    switch (step) {
    case StepId::Partition:
        return "Partitioning failed"sv;
    case StepId::Base:
        return "Base install failed"sv;
    case StepId::Fstab:
        return "fstab generation failed"sv;
    case StepId::SystemSettings:
        return "System settings failed"sv;
    case StepId::Bootloader:
        return "Bootloader installation failed"sv;
    default:
        return {};
    }
}

auto from_expected(std::expected<void, std::string>&& res, StepId step) -> StepOutcome {
    if (res) {
        return StepOutcome{};
    }
    if (fatal_label(step).empty()) {
        return StepOutcome{.status = StepStatus::Warning, .warnings = {std::move(res).error()}};
    }
    return StepOutcome{.status = StepStatus::Failed, .error = std::move(res).error()};
}

auto from_warnings(std::vector<std::string>&& warnings) -> StepOutcome {
    const auto status = warnings.empty() ? StepStatus::Ok : StepStatus::Warning;
    return StepOutcome{.status = status, .warnings = std::move(warnings)};
}

class PacmanProgressObserver final : public gucc::utils::ExecutionObserver {
 public:
    explicit PacmanProgressObserver(const InstallSession& session) noexcept : m_session(session) { }

    void enter(std::size_t index, std::size_t total, std::string_view label) {
        m_index = index;
        m_total = total;
        m_label = std::string{label};
    }

    void on_line([[maybe_unused]] const gucc::utils::CommandInfo& cmd, std::string_view line) noexcept override {
        const auto frac = parse_pacman_progress(line);
        if (!frac || m_total == 0) {
            return;
        }
        const auto total = static_cast<double>(m_total);
        m_session.notify_progress(ProgressEvent{
            .type     = ProgressEventType::Running,
            .message  = m_label,
            .fraction = (static_cast<double>(m_index) + *frac) / total,
        });
    }

 private:
    const InstallSession& m_session;
    std::size_t m_index{};
    std::size_t m_total{};
    std::string m_label;
};

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

auto plan_steps(const InstallContext& ctx) noexcept -> std::vector<StepId> {
    std::vector<StepId> plan;
    // TODO(vnepogodin): generally we don't want to support that,
    // so let it be for the sake of feature parity for now
    if (steps::needs_umount(ctx)) {
        plan.push_back(StepId::Umount);
    }

    // TODO(vnepogodin): look into userver depend upload for dependent modules,
    // although it shouldn't be needed as we would want to require preconditions and prechecks for all values
    // to be already present and called in-order
    plan.append_range(std::initializer_list{
        StepId::Partition,
        StepId::Base,
        StepId::Fstab,
        StepId::EncryptSwap,
        // NOTE(vnepogodin): generally OEM setup should be after all installs, partitions etc
        StepId::SystemSettings,
        StepId::Users,
        StepId::MachineId,
        StepId::Desktop,
        StepId::DesktopConfigure,
    });
    if (ctx.resolved_server) {
        plan.append_range(std::initializer_list{StepId::ServerPackages, StepId::SshKeys, StepId::ServerFirewall});
    }
    plan.append_range(std::initializer_list{StepId::Autologin, StepId::Chwd});
    if (ctx.carry_live_network) {
        plan.push_back(StepId::NetworkCarryover);
    }
    plan.append_range(std::initializer_list{
        StepId::Bootloader,
        StepId::DetectCrypto,
        StepId::EnableServices,
        StepId::FinalValidation,
        StepId::BtrfsSnapshot,
        StepId::Cleanup,
    });
    return plan;
}

auto run_step(StepId step, const InstallRequest& request) noexcept -> StepOutcome {
    auto& ctx = request.ctx;
    switch (step) {
    case StepId::Umount:
        return from_expected(steps::umount(ctx), step);
    case StepId::Partition:
        return from_expected(steps::partition(ctx), step);
    case StepId::Base:
        return from_expected(steps::base(ctx), step);
    case StepId::Fstab:
        return from_expected(steps::fstab(ctx), step);
    case StepId::EncryptSwap:
        return from_warnings(steps::encrypt_swap(ctx));
    case StepId::SystemSettings:
        return from_expected(steps::system_settings(request.sys, ctx), step);
    case StepId::Users:
        return from_warnings(steps::users(request.user, request.root_password, ctx));
    case StepId::MachineId:
        return from_expected(steps::machine_id(ctx), step);
    case StepId::Desktop:
        return from_expected(steps::desktop(ctx), step);
    case StepId::DesktopConfigure:
        return from_expected(steps::desktop_configure(ctx), step);
    case StepId::ServerPackages:
        return from_expected(steps::server_packages(ctx), step);
    case StepId::SshKeys:
        return from_expected(steps::ssh_keys(request.user, ctx), step);
    case StepId::ServerFirewall:
        return from_expected(steps::server_firewall(ctx), step);
    case StepId::Autologin:
        return from_expected(steps::autologin(request.user, ctx), step);
    case StepId::Chwd:
        return from_expected(steps::chwd(ctx), step);
    case StepId::NetworkCarryover:
        if (steps::network_carryover(ctx) < 0) {
            return StepOutcome{.status = StepStatus::Warning, .warnings = {"network connection carryover failed"}};
        }
        return StepOutcome{};
    case StepId::Bootloader:
        return from_expected(steps::bootloader(ctx), step);
    case StepId::DetectCrypto:
        // an undetected layout only means no crypto kernel params
        if (auto res = steps::detect_crypto(ctx); !res) {
            spdlog::debug("detect_crypto: {}", res.error());
        }
        return StepOutcome{};
    case StepId::EnableServices:
        return from_expected(steps::enable_services(ctx), step);
    case StepId::FinalValidation: {
        const auto check = steps::final_validation(ctx);
        return from_warnings(std::ranges::views::concat(check.errors, check.warnings)
            | std::ranges::views::transform([](const std::string& msg) { return fmt::format("final_check: {}", msg); })
            | std::ranges::to<std::vector<std::string>>());
    }
    case StepId::BtrfsSnapshot:
        return from_expected(steps::btrfs_snapshot(ctx), step);
    case StepId::Cleanup:
        return from_warnings(steps::cleanup(ctx));
    }
    return StepOutcome{.status = StepStatus::Failed, .error = "unknown step"};
}

auto run(InstallContext& ctx,
    const SystemSettings& sys,
    const UserSettings& user,
    std::string_view root_password,
    const InstallSession& session) noexcept -> ValidationResult {
    constexpr auto kCancelled = "Cancelled by user"sv;
    auto& runner              = session.runner();

    // reset run state
    runner.reset_cancel();

    // register secrets
    gucc::logger::register_secret(root_password);
    gucc::logger::register_secret(user.password);
    if (const auto* layout = std::get_if<partition_strategy::CreateLayout>(&ctx.strategy);
        layout != nullptr && layout->zfs_setup && layout->zfs_setup->passphrase) {
        gucc::logger::register_secret(*layout->zfs_setup->passphrase);
    }

    const auto plan        = plan_steps(ctx);
    const auto total       = plan.size();
    const auto fraction_at = [total](std::size_t index) {
        return static_cast<double>(index) / static_cast<double>(total);
    };

    PacmanProgressObserver pacman_progress{session};
    const auto progress_handle = runner.add_observer(pacman_progress);

    const InstallRequest request{.ctx = ctx, .sys = sys, .user = user, .root_password = root_password};
    std::vector<std::string> warnings;

    const auto cancelled_at = [&](StepId step, std::size_t index) {
        session.notify_step_end(step, StepStatus::Cancelled, kCancelled);
        session.notify_progress(ProgressEvent{.type = ProgressEventType::Cancelled, .message = std::string{kCancelled}, .fraction = fraction_at(index)});
        return ValidationResult{.success = false, .errors = {std::string{kCancelled}}, .warnings = std::move(warnings)};
    };

    spdlog::info("Install orchestrator starting...");
    session.notify_progress(ProgressEvent{.type = ProgressEventType::Started, .message = "Starting installation...", .fraction = 0.0});

    for (const auto [position, step] : std::ranges::views::enumerate(plan)) {
        const auto index = static_cast<std::size_t>(position);
        const auto label = step_label(step);

        // cleanup always runs once everything before it did
        if (step != StepId::Cleanup && session.cancelled()) {
            return cancelled_at(step, index);
        }

        pacman_progress.enter(index, total, label);
        session.notify_step_begin(step, index, total);
        session.notify_progress(ProgressEvent{.type = ProgressEventType::Running, .message = std::string{label}, .fraction = fraction_at(index)});

        auto outcome = run_step(step, request);
        if (outcome.status == StepStatus::Failed) {
            if (session.cancelled()) {
                return cancelled_at(step, index);
            }
            const auto error = fmt::format("{}: {}", fatal_label(step), outcome.error);
            session.notify_step_end(step, StepStatus::Failed, error);
            session.notify_progress(ProgressEvent{.type = ProgressEventType::Failed, .message = std::string{fatal_label(step)}, .fraction = fraction_at(index)});
            return ValidationResult{.success = false, .errors = {error}, .warnings = std::move(warnings)};
        }

        for (const auto& warning : outcome.warnings) {
            spdlog::warn("{}: {}", label, warning);
        }
        session.notify_step_end(step, outcome.status, {});

        // NOTE: warnings isn't moved here yet
        std::ranges::move(outcome.warnings, std::back_inserter(warnings));
    }

    session.notify_progress(ProgressEvent{.type = ProgressEventType::Completed, .message = "Installation complete!", .fraction = 1.0});
    spdlog::info("Install orchestrator finished.");

    return ValidationResult{
        .success  = true,
        .errors   = {},
        .warnings = std::move(warnings),
    };
}

}  // namespace cachyos::installer
