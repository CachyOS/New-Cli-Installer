#include "gucc/autologin.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/user.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::user {

auto enable_autologin(std::string_view displaymanager, std::string_view username, std::string_view root_mountpoint) noexcept -> bool {
    // TODO(vnepogodin): check for failed commands

    // enable autologin
    if (displaymanager == "gdm"sv) {
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^AutomaticLogin=*/AutomaticLogin={}/g' {}/etc/gdm/custom.conf"), username, root_mountpoint));
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^AutomaticLoginEnable=*/AutomaticLoginEnable=true/g' {}/etc/gdm/custom.conf"), root_mountpoint));
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^TimedLoginEnable=*/TimedLoginEnable=true/g' {}/etc/gdm/custom.conf"), root_mountpoint));
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^TimedLogin=*/TimedLoginEnable={}/g' {}/etc/gdm/custom.conf"), username, root_mountpoint));
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^TimedLoginDelay=*/TimedLoginDelay=0/g' {}/etc/gdm/custom.conf"), root_mountpoint));
    } else if (displaymanager == "lightdm"sv) {
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^#autologin-user=/autologin-user={}/' {}/etc/lightdm/lightdm.conf"), username, root_mountpoint));
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^#autologin-user-timeout=0/autologin-user-timeout=0/' {}/etc/lightdm/lightdm.conf"), root_mountpoint));

        // create autologin group
        if (!user::create_group("autologin"sv, root_mountpoint, true)) {
            spdlog::error("Failed to create autologin group");
            return false;
        }
        // add group to user
        const auto& groupadd_cmd = fmt::format(FMT_COMPILE("gpasswd -a {} autologin"), username);
        if (!utils::arch_chroot_checked(groupadd_cmd, root_mountpoint)) {
            spdlog::error("Failed to add autologin group to user: {}", username);
            return false;
        }
    } else if (displaymanager == "sddm"sv) {
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^User=/User={}/g' {}/etc/sddm.conf"), username, root_mountpoint));
    } else if (displaymanager == "lxdm"sv) {
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^# autologin=dgod/autologin={}/g' {}/etc/lxdm/lxdm.conf"), username, root_mountpoint));
    }

    return true;
}

}  // namespace gucc::user
