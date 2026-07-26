#include "cachyos/packages.hpp"
#include "cachyos/steps.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace cachyos::installer::steps {

auto desktop_configure(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    if (ctx.server_mode || ctx.desktop.empty()) {
        return {};
    }
    if (auto res = configure_desktop_extras(ctx); !res) {
        spdlog::error("configure_desktop_extras: {}", res.error());
        return std::unexpected(fmt::format("configure_desktop_extras: {}", res.error()));
    }
    return {};
}

}  // namespace cachyos::installer::steps
