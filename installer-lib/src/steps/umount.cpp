#include "cachyos/disk.hpp"
#include "cachyos/steps.hpp"

namespace cachyos::installer::steps {

auto umount(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    return umount_partitions(ctx.mountpoint, ctx.zfs_zpool_names, ctx.swap_device);
}

}  // namespace cachyos::installer::steps
