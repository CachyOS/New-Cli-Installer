#include "cachyos/steps.hpp"

// import gucc
#include "gucc/chwd.hpp"
#include "gucc/process.hpp"

#include <spdlog/spdlog.h>

#include <utility>

namespace cachyos::installer::steps {

auto chwd(const InstallContext& ctx,
    LogCallback log_cb,
    std::stop_token stop_token) noexcept -> std::expected<void, std::string> {
    if (!ctx.install_chwd_profiles) {
        return {};
    }
    gucc::utils::default_runner().set_line_sink(std::move(log_cb));
    const std::stop_callback on_cancel(stop_token, [] { gucc::utils::default_runner().cancel(); });
    if (!gucc::chwd::install_available_profiles(ctx.mountpoint)) {
        spdlog::error("chwd: install_available_profiles failed");
        return std::unexpected(std::string{"chwd: install_available_profiles failed"});
    }
    return {};
}

}  // namespace cachyos::installer::steps
