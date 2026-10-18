#include "cachyos/result.hpp"
#include "cachyos/steps.hpp"

// import gucc
#include "gucc/autologin.hpp"
#include "gucc/display_manager.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_literals;

namespace cachyos::installer::steps {

auto autologin(const UserSettings& user, const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    if (!user.autologin) {
        return {};
    }
    if (user.username.empty()) {
        return std::unexpected("autologin requested but username is empty"s);
    }

    const auto dm = gucc::display_manager::detect_installed(ctx.mountpoint);
    if (!dm) {
        return std::unexpected("no display manager installed on target"s);
    }

    const auto dm_name = gucc::display_manager::to_string(*dm);
    return lift(gucc::user::enable_autologin(dm_name, user.username, ctx.mountpoint),
        fmt::format(FMT_COMPILE("enable_autologin failed for dm={} user={}"), dm_name, user.username));
}

}  // namespace cachyos::installer::steps
