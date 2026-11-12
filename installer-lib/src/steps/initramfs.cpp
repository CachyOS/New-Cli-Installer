#include "cachyos/steps.hpp"
#include "detail/result.hpp"

// import gucc
#include "gucc/initcpio.hpp"
#include "gucc/io_utils.hpp"

using namespace std::string_view_literals;

namespace cachyos::installer::steps {

auto initramfs(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    return lift(gucc::utils::arch_chroot_or_error("mkinitcpio -P"sv, ctx.mountpoint, "generate initramfs"sv));
}

auto restore_initramfs_hook(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    return lift(gucc::initcpio::remove_deferral_hook(ctx.mountpoint));
}

}  // namespace cachyos::installer::steps
