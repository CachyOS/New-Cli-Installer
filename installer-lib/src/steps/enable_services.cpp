#include "cachyos/packages.hpp"
#include "cachyos/steps.hpp"

namespace cachyos::installer::steps {

auto enable_services(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    return cachyos::installer::enable_services(ctx);
}

}  // namespace cachyos::installer::steps
