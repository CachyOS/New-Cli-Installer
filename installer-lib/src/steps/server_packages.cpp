#include "cachyos/packages.hpp"
#include "cachyos/steps.hpp"

#include "gucc/string_utils.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace cachyos::installer::steps {

auto server_packages(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    if (!ctx.resolved_server) {
        return {};
    }
    const auto& profile = *ctx.resolved_server;

    // whatever merged packages
    auto packages    = profile.packages;
    const auto extra = resolve_netinstall_packages(ctx);
    packages.append_range(extra);

    spdlog::info("Installing server profile '{}' packages: '{}'", profile.id, gucc::utils::join(packages, ' '));
    if (auto res = install_packages(packages, ctx.mountpoint, ctx.hostcache); !res) {
        spdlog::error("server_packages: {}", res.error());
        return std::unexpected(fmt::format("server_packages: {}", res.error()));
    }
    return {};
}

}  // namespace cachyos::installer::steps
