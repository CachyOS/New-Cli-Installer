#pragma once

#include <functional>   // for function
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace cachyos::installer {

/// Single welcome-page requirement entry.
struct Requirement {
    std::string id;
    std::string message_ok;
    std::string message_failed;
    bool satisfied{};
    bool mandatory{};
};

/// Tunables for the built-in checkers and the active check set.
struct RequirementsConfig {
    double required_storage_gib{8.0};
    double required_ram_gib{2.5};
    std::string internet_check_url{"https://cachyos.org"};
    std::vector<std::string> checks{
        "root", "uefi", "ram", "storage", "power", "internet"};
    std::vector<std::string> mandatory{"root", "ram", "internet"};
};

/// Signature for a requirement checker.
using RequirementChecker = std::function<Requirement(const RequirementsConfig&)>;

/// Registers (or replaces) a checker. Thread-safe.
void register_requirement(std::string_view id, RequirementChecker checker) noexcept;

/// Snapshot of currently registered checker ids.
[[nodiscard]] auto registered_requirement_ids() noexcept
    -> std::vector<std::string>;

/// Runs the checks in `cfg.checks` order, skipping unknown ids with a warning.
[[nodiscard]] auto check_requirements(const RequirementsConfig& cfg) noexcept
    -> std::vector<Requirement>;

/// Registers the built-in checkers.
void register_builtin_requirements() noexcept;

}  // namespace cachyos::installer
