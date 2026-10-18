#include "cachyos/result.hpp"
#include "cachyos/steps.hpp"

// import gucc
#include "gucc/firewall.hpp"

namespace cachyos::installer::steps {

auto server_firewall(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    if (!ctx.resolved_server) {
        return {};
    }
    const auto& profile = *ctx.resolved_server;
    return lift(gucc::firewall::configure_server_firewall(ctx.mountpoint, profile.firewall_tcp_ports, profile.firewall_udp_ports));
}

}  // namespace cachyos::installer::steps
