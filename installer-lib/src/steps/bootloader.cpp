#include "cachyos/bootloader.hpp"
#include "cachyos/steps.hpp"

// import gucc
#include "gucc/process.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <utility>

namespace cachyos::installer::steps {

auto bootloader(const InstallContext& ctx,
    LogCallback log_cb,
    std::stop_token stop_token) noexcept -> std::expected<void, std::string> {
    gucc::utils::default_runner().set_line_sink(std::move(log_cb));
    const std::stop_callback on_cancel(stop_token, [] { gucc::utils::default_runner().cancel(); });
    if (auto res = install_bootloader(ctx); !res) {
        spdlog::error("install_bootloader: {}", res.error());
        return std::unexpected(fmt::format("install_bootloader: {}", res.error()));
    }
    return {};
}

}  // namespace cachyos::installer::steps
