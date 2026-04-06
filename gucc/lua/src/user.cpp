#include "gucc/lua/bindings.hpp"

#include "gucc/autologin.hpp"
#include "gucc/user.hpp"

namespace gucc::lua {

void register_user(sol::table& gucc_table) {
    auto user = make_subtable(gucc_table, "user");

    user["set_hostname"] = [](std::string_view hostname, std::string_view mountpoint) -> bool {
        return gucc::user::set_hostname(hostname, mountpoint);
    };

    user["set_hosts"] = [](std::string_view hostname, std::string_view mountpoint) -> bool {
        return gucc::user::set_hosts(hostname, mountpoint);
    };

    user["set_root_password"] = [](std::string_view password, std::string_view mountpoint) -> bool {
        return gucc::user::set_root_password(password, mountpoint);
    };

    user["create_new_user"] = [](sol::table user_table, sol::object groups_obj, std::string_view mountpoint) -> bool {
        std::string username      = user_table.get_or<std::string>("username", "");
        std::string password      = user_table.get_or<std::string>("password", "");
        std::string shell         = user_table.get_or<std::string>("shell", "/bin/bash");
        std::string sudoers_group = user_table.get_or<std::string>("sudoers_group", "wheel");

        std::vector<std::string> default_groups;
        if (groups_obj.is<sol::table>()) {
            default_groups = groups_obj.as<std::vector<std::string>>();
        }
        gucc::user::UserInfo user_info{
            .username      = username,
            .password      = password,
            .shell         = shell,
            .sudoers_group = sudoers_group,
        };
        return gucc::user::create_new_user(user_info, default_groups, mountpoint);
    };

    user["set_user_password"] = [](std::string_view username, std::string_view password, std::string_view mountpoint) -> bool {
        return gucc::user::set_user_password(username, password, mountpoint);
    };

    user["create_group"] = [](std::string_view group, std::string_view mountpoint, sol::object is_system_obj) -> bool {
        bool is_system = false;
        if (is_system_obj.is<bool>()) {
            is_system = is_system_obj.as<bool>();
        }
        return gucc::user::create_group(group, mountpoint, is_system);
    };

    user["setup_user_environment"] = [](sol::table info_table, std::string_view mountpoint) -> bool {
        std::string root_password   = info_table.get_or<std::string>("root_password", "");
        std::string hostname        = info_table.get_or<std::string>("hostname", "");
        std::string display_manager = info_table.get_or<std::string>("display_manager", "");
        bool autologin              = lua_get_or<bool>(info_table, "autologin", false);

        std::vector<gucc::user::UserInfo> users_info;
        if (sol::object users = info_table["users"]; users.is<sol::table>()) {
            users.as<sol::table>().for_each([&](sol::object, sol::object val) {
                if (val.is<sol::table>()) {
                    auto t                    = val.as<sol::table>();
                    std::string username      = t.get_or<std::string>("username", "");
                    std::string password      = t.get_or<std::string>("password", "");
                    std::string shell         = t.get_or<std::string>("shell", "/bin/bash");
                    std::string sudoers_group = t.get_or<std::string>("sudoers_group", "wheel");
                    users_info.push_back(gucc::user::UserInfo{
                        .username      = username,
                        .password      = password,
                        .shell         = shell,
                        .sudoers_group = sudoers_group,
                    });
                }
            });
        }
        std::vector<std::string> default_groups;
        if (sol::object groups = info_table["default_groups"]; groups.is<sol::table>()) {
            default_groups = groups.as<std::vector<std::string>>();
        }
        gucc::user::UserAllInfo info{
            .users_info      = std::move(users_info),
            .default_groups  = std::move(default_groups),
            .root_password   = root_password,
            .hostname        = hostname,
            .display_manager = display_manager,
            .autologin       = autologin,
        };
        return gucc::user::setup_user_environment(info, mountpoint);
    };

    user["enable_autologin"] = [](std::string_view displaymanager, std::string_view username, std::string_view root_mountpoint) -> bool {
        return gucc::user::enable_autologin(displaymanager, username, root_mountpoint);
    };
}

}  // namespace gucc::lua
