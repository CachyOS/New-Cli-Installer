#include "cachyos/config.hpp"
#include "cachyos/steps.hpp"

namespace cachyos::installer::steps {

auto system_settings(const SystemSettings& sys, const InstallContext& ctx) noexcept
    -> std::expected<void, std::string> {
    return apply_system_settings(sys, ctx.mountpoint);
}

}  // namespace cachyos::installer::steps
