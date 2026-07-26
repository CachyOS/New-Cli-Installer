#include "cachyos/bootloader.hpp"
#include "cachyos/steps.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace cachyos::installer::steps {

auto bootloader(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    if (auto res = install_bootloader(ctx); !res) {
        spdlog::error("install_bootloader: {}", res.error());
        return std::unexpected(fmt::format("install_bootloader: {}", res.error()));
    }
    return {};
}

}  // namespace cachyos::installer::steps
