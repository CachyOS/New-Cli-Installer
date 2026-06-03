#include "cachyos/packages.hpp"
#include "cachyos/steps.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace cachyos::installer::steps {

auto enable_services(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    if (auto res = cachyos::installer::enable_services(ctx); !res) {
        spdlog::error("enable_services: {}", res.error());
        return std::unexpected(fmt::format("enable_services: {}", res.error()));
    }
    return {};
}

}  // namespace cachyos::installer::steps
