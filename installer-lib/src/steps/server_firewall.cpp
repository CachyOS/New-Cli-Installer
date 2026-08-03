#include "cachyos/steps.hpp"

// import gucc
#include "gucc/firewall.hpp"

#include <fmt/format.h>

namespace cachyos::installer::steps {

auto server_firewall(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    if (!ctx.resolved_server) {
        return {};
    }
    const auto& profile = *ctx.resolved_server;

    if (auto res = gucc::firewall::configure_server_firewall(ctx.mountpoint, profile.firewall_tcp_ports, profile.firewall_udp_ports); !res) {
        return std::unexpected(fmt::format("server_firewall: {}", res.error().context));
    }
    return {};
}

}  // namespace cachyos::installer::steps
