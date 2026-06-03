#include "cachyos/disk.hpp"
#include "cachyos/steps.hpp"

namespace cachyos::installer::steps {

auto fstab(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    return generate_fstab(ctx.mountpoint);
}

}  // namespace cachyos::installer::steps
