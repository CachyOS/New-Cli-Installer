#include "cachyos/packages.hpp"
#include "cachyos/steps.hpp"

// import gucc
#include "gucc/subprocess.hpp"

#include <utility>

namespace cachyos::installer::steps {

auto base(const InstallContext& ctx,
    LogCallback log_cb,
    std::stop_token stop_token) noexcept -> std::expected<void, std::string> {
    gucc::utils::SubProcess child;
    child.set_log_line_callback(std::move(log_cb));
    const std::stop_callback on_cancel(stop_token, [&child] { child.terminate(); });
    return install_base(ctx, child);
}

}  // namespace cachyos::installer::steps
