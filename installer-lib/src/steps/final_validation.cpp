#include "cachyos/steps.hpp"
#include "cachyos/validation.hpp"

#include <spdlog/spdlog.h>

namespace cachyos::installer::steps {

auto final_validation(const InstallContext& ctx) noexcept -> ValidationResult {
    auto check = final_check(ctx);
    for (const auto& err : check.errors) {
        spdlog::error("final_check: {}", err);
    }
    for (const auto& warn : check.warnings) {
        spdlog::warn("final_check: {}", warn);
    }
    return check;
}

}  // namespace cachyos::installer::steps
