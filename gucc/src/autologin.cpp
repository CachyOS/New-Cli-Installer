#include "gucc/autologin.hpp"
#include "gucc/conf.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/user.hpp"

#include <filesystem>        // for exists
#include <initializer_list>  // for initializer_list
#include <string>            // for string
#include <system_error>      // for error_code
#include <utility>           // for pair

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace {

auto set_config_values(std::string_view path, std::string_view section, std::initializer_list<std::pair<std::string_view, std::string_view>> values) noexcept -> gucc::Result<void> {
    std::error_code err{};
    auto content = std::filesystem::exists(path, err) ? gucc::file_utils::read_whole_file(path) : std::string{};
    for (const auto& [key, value] : values) {
        content = gucc::conf::set_value(content, section, key, value);
    }
    return gucc::file_utils::write_file(path, content);
}

}  // namespace

namespace gucc::user {

auto enable_autologin(std::string_view displaymanager, std::string_view username, std::string_view root_mountpoint) noexcept -> Result<void> {
    if (displaymanager == "gdm"sv) {
        return set_config_values(utils::target_path(root_mountpoint, "/etc/gdm/custom.conf"sv), "daemon"sv,
            {{"AutomaticLoginEnable"sv, "True"sv}, {"AutomaticLogin"sv, username}});
    }
    if (displaymanager == "lightdm"sv) {
        if (auto res = set_config_values(utils::target_path(root_mountpoint, "/etc/lightdm/lightdm.conf"sv), "Seat:*"sv,
                {{"autologin-user"sv, username}, {"autologin-user-timeout"sv, "0"sv}});
            !res) {
            return res;
        }
        if (auto res = user::create_group("autologin"sv, root_mountpoint, true); !res) {
            return res;
        }
        return utils::arch_chroot_or_error({"/usr/bin/gpasswd"s, "-a"s, std::string{username}, "autologin"s}, root_mountpoint, "add user to autologin group"sv);
    }
    if (displaymanager == "plasmalogin"sv) {
        return set_config_values(utils::target_path(root_mountpoint, "/etc/plasmalogin.conf"sv), "Autologin"sv, {{"User"sv, username}});
    }
    if (displaymanager == "sddm"sv) {
        return set_config_values(utils::target_path(root_mountpoint, "/etc/sddm.conf"sv), "Autologin"sv, {{"User"sv, username}});
    }
    if (displaymanager == "lxdm"sv) {
        return set_config_values(utils::target_path(root_mountpoint, "/etc/lxdm/lxdm.conf"sv), "base"sv, {{"autologin"sv, username}});
    }
    return {};
}

}  // namespace gucc::user
