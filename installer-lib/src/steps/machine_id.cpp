#include "cachyos/result.hpp"
#include "cachyos/steps.hpp"

// import gucc
#include "gucc/machine_id.hpp"

namespace cachyos::installer::steps {

auto machine_id(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    return lift(gucc::machine_id::reset(ctx.mountpoint), "machine_id reset");
}

}  // namespace cachyos::installer::steps
