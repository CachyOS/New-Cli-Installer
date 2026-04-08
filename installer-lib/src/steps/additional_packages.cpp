#include "cachyos/packages.hpp"
#include "cachyos/steps.hpp"

namespace cachyos::installer::steps {

auto additional_packages(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    return install_packages(ctx.additional_packages, ctx.mountpoint, ctx.hostcache);
}

}  // namespace cachyos::installer::steps
