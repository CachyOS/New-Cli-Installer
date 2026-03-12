#include "cachyos/validation.hpp"

#include <expected>     // for unexpected
#include <string>       // for string
#include <string_view>  // for string_view

#include <spdlog/spdlog.h>

namespace cachyos::installer {

auto check_mount(std::string_view /*mountpoint*/) noexcept -> bool {
    return false;
}

auto check_base_installed(std::string_view /*mountpoint*/) noexcept -> bool {
    return false;
}

auto final_check(const InstallContext& /*ctx*/) noexcept -> ValidationResult {
    return {.success = false, .errors = {"not yet implemented"}, .warnings = {}};
}

}  // namespace cachyos::installer
