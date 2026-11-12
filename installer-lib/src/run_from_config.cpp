#include "cachyos/run_from_config.hpp"
#include "cachyos/orchestrator.hpp"
#include "cachyos/system.hpp"

// import gucc
#include "gucc/cpu.hpp"

#include <chrono>   // for chrono_literals
#include <string>   // for string
#include <utility>  // for move

#include <fmt/compile.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

namespace cachyos::installer {

auto run_from_config(const InstallerConfig& config, InstallSession& session) noexcept -> ValidationResult {
    const auto refuse = [](std::string error) {
        return ValidationResult{.success = false, .errors = {std::move(error)}, .warnings = {}};
    };

    // validate before touching anything
    if (auto valid = validate_headless_config(config); !valid) {
        return refuse(fmt::format(FMT_COMPILE("config validation failed: {}"), fmt::join(valid.error(), "; ")));
    }

    using namespace std::chrono_literals;
    if (!wait_for_connection(15s)) {
        return refuse("an active network connection is required");
    }

    spdlog::info("isa_levels:={}", gucc::cpu::get_isa_levels());
    if (auto repo = install_cachyos_repo(); !repo) {
        spdlog::warn("install_cachyos_repo: {}", repo.error());
    }

    auto inputs = installer_config_to_inputs(config);
    if (!inputs) {
        return refuse(fmt::format("config conversion failed: {}", inputs.error()));
    }

    return run(inputs->ctx, inputs->sys, inputs->user, inputs->root_password, session);
}

}  // namespace cachyos::installer
