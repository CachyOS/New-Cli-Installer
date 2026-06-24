#include "cachyos/steps.hpp"

// import gucc
#include "gucc/machine_id.hpp"

#include <spdlog/spdlog.h>

using namespace std::string_literals;

namespace cachyos::installer::steps {

auto machine_id(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    if (auto res = gucc::machine_id::reset(ctx.mountpoint); !res) {
        spdlog::warn("machine_id::reset failed for '{}': {}", ctx.mountpoint, gucc::to_string(res.error()));
        return std::unexpected("machine_id reset failed"s);
    }
    return {};
}

}  // namespace cachyos::installer::steps
