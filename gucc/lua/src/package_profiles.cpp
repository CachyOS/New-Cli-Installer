#include "gucc/lua/bindings.hpp"

#include "gucc/package_profiles.hpp"

namespace gucc::lua {

void register_package_profiles(sol::table& gucc_table) {
    auto profiles = make_subtable(gucc_table, "profiles");

    profiles["parse_base_profiles"] = [gucc_table](std::string_view config_content) -> sol::object {
        sol::state_view lua(gucc_table.lua_state());
        auto result = gucc::profile::parse_base_profiles(config_content);
        if (!result) {
            return sol::lua_nil;
        }
        auto table                     = lua.create_table();
        table["base_packages"]         = sol::as_table(result->base_packages);
        table["base_desktop_packages"] = sol::as_table(result->base_desktop_packages);

        auto services_to_table = [&](const std::vector<gucc::profile::ServiceEntry>& entries) -> sol::table {
            auto svc_table = lua.create_table();
            for (size_t i = 0; i < entries.size(); ++i) {
                auto entry               = lua.create_table();
                entry["name"]            = entries[i].name;
                entry["action"]          = entries[i].action == gucc::profile::ServiceAction::Enable ? "enable" : "disable";
                entry["is_user_service"] = entries[i].is_user_service;
                entry["is_urgent"]       = entries[i].is_urgent;
                svc_table[i + 1]         = entry;
            }
            return svc_table;
        };
        table["base_services"]         = services_to_table(result->base_services);
        table["base_desktop_services"] = services_to_table(result->base_desktop_services);
        return table;
    };

    profiles["parse_desktop_profiles"] = [gucc_table](std::string_view config_content) -> sol::object {
        sol::state_view lua(gucc_table.lua_state());
        auto result = gucc::profile::parse_desktop_profiles(config_content);
        if (!result) {
            return sol::lua_nil;
        }
        auto table = lua.create_table();
        for (size_t i = 0; i < result->size(); ++i) {
            auto profile            = lua.create_table();
            profile["profile_name"] = (*result)[i].profile_name;
            profile["packages"]     = sol::as_table((*result)[i].packages);

            table[i + 1] = profile;
        }
        return table;
    };

    profiles["parse_net_profiles"] = [gucc_table](std::string_view config_content) -> sol::object {
        sol::state_view lua(gucc_table.lua_state());
        auto result = gucc::profile::parse_net_profiles(config_content);
        if (!result) {
            return sol::lua_nil;
        }

        auto table                    = lua.create_table();
        auto base                     = lua.create_table();
        base["base_packages"]         = sol::as_table(result->base_profiles.base_packages);
        base["base_desktop_packages"] = sol::as_table(result->base_profiles.base_desktop_packages);
        table["base_profiles"]        = base;

        auto desktops = lua.create_table();
        for (size_t i = 0; i < result->desktop_profiles.size(); ++i) {
            auto profile            = lua.create_table();
            profile["profile_name"] = result->desktop_profiles[i].profile_name;
            profile["packages"]     = sol::as_table(result->desktop_profiles[i].packages);

            desktops[i + 1] = profile;
        }
        table["desktop_profiles"] = desktops;
        return table;
    };
}

}  // namespace gucc::lua
