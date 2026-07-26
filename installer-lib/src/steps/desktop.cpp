#include "cachyos/packages.hpp"
#include "cachyos/steps.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace cachyos::installer::steps {

auto desktop(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    if (ctx.server_mode || ctx.desktop.empty()) {
        return {};
    }
    if (auto res = install_desktop_packages(ctx.desktop, ctx); !res) {
        spdlog::error("install_desktop_packages: {}", res.error());
        return std::unexpected(fmt::format("install_desktop_packages: {}", res.error()));
    }
    return {};
}

}  // namespace cachyos::installer::steps
