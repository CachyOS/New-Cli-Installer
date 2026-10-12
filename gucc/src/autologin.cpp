#include "gucc/autologin.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/user.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::user {

auto enable_autologin(std::string_view displaymanager, std::string_view username, std::string_view root_mountpoint) noexcept -> Result<void> {
    if (displaymanager == "gdm"sv) {
        const auto conf = fmt::format(FMT_COMPILE("{}/etc/gdm/custom.conf"), root_mountpoint);
        for (const auto& sed : {
            fmt::format(FMT_COMPILE("sed -i 's/^AutomaticLogin=*/AutomaticLogin={}/g' {}"), username, conf),
            fmt::format(FMT_COMPILE("sed -i 's/^AutomaticLoginEnable=*/AutomaticLoginEnable=true/g' {}"), conf),
            fmt::format(FMT_COMPILE("sed -i 's/^TimedLoginEnable=*/TimedLoginEnable=true/g' {}"), conf),
            fmt::format(FMT_COMPILE("sed -i 's/^TimedLogin=*/TimedLogin={}/g' {}"), username, conf),
            fmt::format(FMT_COMPILE("sed -i 's/^TimedLoginDelay=*/TimedLoginDelay=0/g' {}"), conf),
        }) {
            if (!utils::exec_checked(sed)) {
                return make_error(ErrorCode::SubprocessFailed, fmt::format("autologin: failed to configure GDM"));
            }
        }
    } else if (displaymanager == "lightdm"sv) {
        const auto conf = fmt::format(FMT_COMPILE("{}/etc/lightdm/lightdm.conf"), root_mountpoint);
        if (!utils::exec_checked(fmt::format(FMT_COMPILE("sed -i 's/^#autologin-user=/autologin-user={}/' {}"), username, conf))) {
            return make_error(ErrorCode::SubprocessFailed, "autologin: failed to configure LightDM");
        }
        if (!utils::exec_checked(fmt::format(FMT_COMPILE("sed -i 's/^#autologin-user-timeout=0/autologin-user-timeout=0/' {}"), conf))) {
            return make_error(ErrorCode::SubprocessFailed, "autologin: failed to configure LightDM timeout");
        }

        if (auto res = create_group("autologin"sv, root_mountpoint, true); !res) return res;
        if (!utils::arch_chroot_checked(fmt::format(FMT_COMPILE("gpasswd -a {} autologin"), username), root_mountpoint)) {
            return make_error(ErrorCode::SubprocessFailed, fmt::format("autologin: failed to add user '{}' to autologin group", username));
        }
    } else if (displaymanager == "plasmalogin"sv) {
        const auto conf = fmt::format(FMT_COMPILE("{}/etc/plasmalogin.conf"), root_mountpoint);
        if (!utils::exec_checked(fmt::format(FMT_COMPILE("sed -i 's/^User=/User={}/g' {}"), username, conf))) {
            return make_error(ErrorCode::SubprocessFailed, "autologin: failed to configure Plasma Login");
        }
    } else if (displaymanager == "sddm"sv) {
        const auto conf = fmt::format(FMT_COMPILE("{}/etc/sddm.conf"), root_mountpoint);
        if (!utils::exec_checked(fmt::format(FMT_COMPILE("sed -i 's/^User=/User={}/g' {}"), username, conf))) {
            return make_error(ErrorCode::SubprocessFailed, "autologin: failed to configure SDDM");
        }
    } else if (displaymanager == "lxdm"sv) {
        const auto conf = fmt::format(FMT_COMPILE("{}/etc/lxdm/lxdm.conf"), root_mountpoint);
        if (!utils::exec_checked(fmt::format(FMT_COMPILE("sed -i 's/^# autologin=dgod/autologin={}/g' {}"), username, conf))) {
            return make_error(ErrorCode::SubprocessFailed, "autologin: failed to configure LXDM");
        }
    }

    return {};
}

}  // namespace gucc::user
