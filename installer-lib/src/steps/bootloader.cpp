#include "cachyos/bootloader.hpp"
#include "cachyos/steps.hpp"

namespace cachyos::installer::steps {

auto bootloader(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    return install_bootloader(ctx);
}

}  // namespace cachyos::installer::steps
