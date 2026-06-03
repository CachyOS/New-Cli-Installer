#include "cachyos/config.hpp"
#include "cachyos/steps.hpp"

// import gucc
#include "gucc/subprocess.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <utility>

namespace cachyos::installer::steps {

auto users(const UserSettings& user,
    std::string_view root_password,
    const InstallContext& ctx,
    LogCallback log_cb,
    std::stop_token stop_token) noexcept -> std::vector<std::string> {
    std::vector<std::string> warnings;

    if (auto res = set_root_password(root_password, ctx.mountpoint); !res) {
        spdlog::error("set_root_password: {}", res.error());
        warnings.emplace_back(fmt::format("set_root_password: {}", res.error()));
    }

    gucc::utils::SubProcess child;
    child.set_log_line_callback(std::move(log_cb));
    const std::stop_callback on_cancel(stop_token, [&child] { child.terminate(); });
    if (auto res = create_user(user, ctx.mountpoint, ctx.hostcache, child); !res) {
        spdlog::error("create_user: {}", res.error());
        warnings.emplace_back(fmt::format("create_user: {}", res.error()));
    }
    return warnings;
}

}  // namespace cachyos::installer::steps
