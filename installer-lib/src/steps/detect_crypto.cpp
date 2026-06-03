#include "cachyos/crypto.hpp"
#include "cachyos/steps.hpp"

#include <spdlog/spdlog.h>

#include <utility>

namespace cachyos::installer::steps {

auto detect_crypto(InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    auto res = detect_crypto_root(ctx.mountpoint);
    if (!res) {
        spdlog::warn("detect_crypto_root: {}", res.error());
        return std::unexpected(res.error());
    }
    ctx.crypto.is_luks   = res->is_luks;
    ctx.crypto.is_lvm    = res->is_lvm;
    ctx.crypto.luks_dev  = std::move(res->luks_dev);
    ctx.crypto.luks_name = std::move(res->luks_name);
    ctx.crypto.luks_uuid = std::move(res->luks_uuid);
    return {};
}

}  // namespace cachyos::installer::steps
