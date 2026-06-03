#include "cachyos/steps.hpp"

// import gucc
#include "gucc/network.hpp"

#include <spdlog/spdlog.h>

#include <string_view>

using namespace std::string_view_literals;

namespace {
constexpr auto kHostNmDir = "/etc/NetworkManager/system-connections"sv;
}  // namespace

namespace cachyos::installer::steps {

auto network_carryover(const InstallContext& ctx) noexcept -> std::int32_t {
    const auto copied = gucc::network::copy_connections_from(kHostNmDir, ctx.mountpoint);
    if (copied < 0) {
        spdlog::warn("network: copy_connections_from failed");
    }
    return copied;
}

}  // namespace cachyos::installer::steps
