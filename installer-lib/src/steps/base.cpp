#include "cachyos/packages.hpp"
#include "cachyos/steps.hpp"

namespace cachyos::installer::steps {

auto base(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    return install_base(ctx);
}

}  // namespace cachyos::installer::steps
