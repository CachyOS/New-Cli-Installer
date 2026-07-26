#include "cachyos/steps.hpp"

// import gucc
#include "gucc/chwd.hpp"

#include <spdlog/spdlog.h>

namespace cachyos::installer::steps {

auto chwd(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    if (!ctx.install_chwd_profiles) {
        return {};
    }
    if (!gucc::chwd::install_available_profiles(ctx.mountpoint)) {
        spdlog::error("chwd: install_available_profiles failed");
        return std::unexpected(std::string{"chwd: install_available_profiles failed"});
    }
    return {};
}

}  // namespace cachyos::installer::steps
