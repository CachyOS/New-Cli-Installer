#include "cachyos/steps.hpp"

// import gucc
#include "cachyos/result.hpp"
#include "gucc/chwd.hpp"

namespace cachyos::installer::steps {

auto chwd(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    if (!ctx.install_chwd_profiles) {
        return {};
    }
    return lift(gucc::chwd::install_available_profiles(ctx.mountpoint));
}

}  // namespace cachyos::installer::steps
