#include "cachyos/packages.hpp"
#include "cachyos/steps.hpp"

namespace cachyos::installer::steps {

auto has_desktop(const InstallContext& ctx) noexcept -> bool {
    return !ctx.server_mode && !ctx.desktop.empty();
}

auto desktop(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    if (!has_desktop(ctx)) {
        return {};
    }
    return install_desktop_packages(ctx.desktop, ctx);
}

}  // namespace cachyos::installer::steps
