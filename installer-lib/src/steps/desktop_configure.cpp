#include "cachyos/packages.hpp"
#include "cachyos/steps.hpp"

namespace cachyos::installer::steps {

auto desktop_configure(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    if (!has_desktop(ctx)) {
        return {};
    }
    return configure_desktop_extras(ctx);
}

}  // namespace cachyos::installer::steps
