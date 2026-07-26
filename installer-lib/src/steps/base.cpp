#include "cachyos/packages.hpp"
#include "cachyos/steps.hpp"

// import gucc
#include "gucc/process.hpp"

#include <utility>

namespace cachyos::installer::steps {

auto base(const InstallContext& ctx,
    LogCallback log_cb,
    std::stop_token stop_token) noexcept -> std::expected<void, std::string> {
    gucc::utils::default_runner().set_line_sink(std::move(log_cb));
    const std::stop_callback on_cancel(stop_token, [] { gucc::utils::default_runner().cancel(); });
    return install_base(ctx);
}

}  // namespace cachyos::installer::steps
