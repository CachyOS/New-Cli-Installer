#pragma once

#include "cachyos/session.hpp"
#include "cachyos/types.hpp"

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace cachyos::installer {

/// Parses a pacman-style "(  N/M)" progress prefix from a log line.
[[nodiscard]] auto parse_pacman_progress(std::string_view line) noexcept -> std::optional<double>;

struct InstallRequest {
    InstallContext& ctx;
    const SystemSettings& sys;
    const UserSettings& user;
    std::string_view root_password;
};

struct StepOutcome {
    StepStatus status{StepStatus::Ok};
    std::vector<std::string> warnings{};
    std::string error{};
};

/// Steps that don't match required ctx are skipped.
[[nodiscard]] auto plan_steps(const InstallContext& ctx) noexcept -> std::vector<StepId>;

/// Does not run steps the step depends on.
[[nodiscard]] auto run_step(StepId step, const InstallRequest& request) noexcept -> StepOutcome;

[[nodiscard]] auto run(InstallContext& ctx,
    const SystemSettings& sys,
    const UserSettings& user,
    std::string_view root_password,
    const InstallSession& session) noexcept -> ValidationResult;

}  // namespace cachyos::installer
