#include "cachyos/packages.hpp"
#include "cachyos/steps.hpp"

#include <spdlog/spdlog.h>

namespace cachyos::installer::steps {

auto server_packages(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    if (!ctx.resolved_server) {
        return {};
    }
    spdlog::info("Installing server profile '{}' packages", ctx.resolved_server->id);
    return install_with_netinstall(ctx.resolved_server->packages, ctx);
}

}  // namespace cachyos::installer::steps
