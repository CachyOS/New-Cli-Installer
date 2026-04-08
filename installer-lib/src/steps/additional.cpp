#include "cachyos/packages.hpp"
#include "cachyos/steps.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace cachyos::installer::steps {

auto additional(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string> {
    if (ctx.additional_packages.empty()) {
        return {};
    }
    if (auto res = install_additional(ctx); !res) {
        spdlog::error("install_additional: {}", res.error());
        return std::unexpected(fmt::format("install_additional: {}", res.error()));
    }
    return {};
}

}  // namespace cachyos::installer::steps
