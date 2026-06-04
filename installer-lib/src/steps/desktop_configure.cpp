#include "cachyos/packages.hpp"
#include "cachyos/steps.hpp"

// import gucc
#include "gucc/subprocess.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <utility>

namespace cachyos::installer::steps {

auto desktop_configure(const InstallContext& ctx,
    LogCallback log_cb,
    std::stop_token stop_token) noexcept -> std::expected<void, std::string> {
    if (ctx.server_mode || ctx.desktop.empty()) {
        return {};
    }
    gucc::utils::SubProcess child;
    child.set_log_line_callback(std::move(log_cb));
    const std::stop_callback on_cancel(stop_token, [&child] { child.terminate(); });
    if (auto res = configure_desktop_extras(ctx, child); !res) {
        spdlog::error("configure_desktop_extras: {}", res.error());
        return std::unexpected(fmt::format("configure_desktop_extras: {}", res.error()));
    }
    return {};
}

}  // namespace cachyos::installer::steps
