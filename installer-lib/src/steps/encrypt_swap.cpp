#include "cachyos/steps.hpp"

// import gucc
#include "gucc/luks_swap.hpp"

#include <spdlog/spdlog.h>

namespace cachyos::installer::steps {

auto encrypt_swap(const InstallContext& ctx) noexcept -> std::vector<std::string> {
    std::vector<std::string> warnings;
    if (!ctx.encrypt_swap || ctx.swap_device.empty()) {
        return warnings;
    }
    const gucc::luks_swap::RandomKeyConfig swap_cfg{
        .source_device = ctx.swap_device,
    };
    if (!gucc::luks_swap::add_crypttab_entry(swap_cfg, ctx.mountpoint)) {
        spdlog::warn("luks_swap: failed to add crypttab entry for {}", ctx.swap_device);
        warnings.emplace_back("luks_swap crypttab entry failed");
    }
    if (!gucc::luks_swap::replace_swap_in_fstab(swap_cfg.mapper_name, ctx.mountpoint)) {
        spdlog::warn("luks_swap: failed to rewrite fstab swap entry for /dev/mapper/{}",
            swap_cfg.mapper_name);
        warnings.emplace_back("luks_swap fstab rewrite failed");
    }
    return warnings;
}

}  // namespace cachyos::installer::steps
