#include "cachyos/steps.hpp"

// import gucc
#include "gucc/machine_id.hpp"

#include <spdlog/spdlog.h>

using namespace std::string_literals;

namespace cachyos::installer::steps {

auto machine_id(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    if (!gucc::machine_id::reset(ctx.mountpoint)) {
        spdlog::warn("machine_id::reset failed for '{}'", ctx.mountpoint);
        return std::unexpected("machine_id reset failed"s);
    }
    return {};
}

}  // namespace cachyos::installer::steps
