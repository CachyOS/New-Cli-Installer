#include "cachyos/steps.hpp"

// import gucc
#include "gucc/chwd.hpp"
#include "gucc/subprocess.hpp"

#include <spdlog/spdlog.h>

#include <utility>

namespace cachyos::installer::steps {

auto chwd(const InstallContext& ctx,
    LogCallback log_cb,
    std::stop_token stop_token) noexcept -> std::expected<void, std::string> {
    if (!ctx.install_chwd_profiles) {
        return {};
    }
    gucc::utils::SubProcess child;
    child.set_log_line_callback(std::move(log_cb));
    const std::stop_callback on_cancel(stop_token, [&child] { child.terminate(); });
    if (!gucc::chwd::install_available_profiles(ctx.mountpoint, child)) {
        spdlog::error("chwd: install_available_profiles failed");
        return std::unexpected(std::string{"chwd: install_available_profiles failed"});
    }
    return {};
}

}  // namespace cachyos::installer::steps
