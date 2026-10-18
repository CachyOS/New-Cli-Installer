#include "cachyos/steps.hpp"
#include "cachyos/validation.hpp"

namespace cachyos::installer::steps {

auto final_validation(const InstallContext& ctx) noexcept -> ValidationResult {
    return final_check(ctx);
}

}  // namespace cachyos::installer::steps
