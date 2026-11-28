#include "cachyos/steps.hpp"
#include "detail/result.hpp"

// import gucc
#include "gucc/autologin.hpp"
#include "gucc/display_manager.hpp"

#include <optional>     // for optional
#include <string_view>  // for string_view

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
    // TODO(vnepogodin): context keeps "no desktop" as empty string
    const auto desktop = ctx.desktop.empty() ? std::nullopt : std::optional<std::string_view>{ctx.desktop};
    return lift(gucc::user::enable_autologin(dm_name, user.username, ctx.mountpoint, desktop),
        fmt::format(FMT_COMPILE("enable_autologin failed for dm={} user={}"), dm_name, user.username));
}

}  // namespace cachyos::installer::steps
