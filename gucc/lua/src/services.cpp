#include "gucc/lua/bindings.hpp"

#include "gucc/autologin.hpp"
#include "gucc/systemd_services.hpp"

namespace gucc::lua {

void register_services(sol::table& gucc_table) {
    auto services = make_subtable(gucc_table, "services");

    services["enable"] = [](std::string_view service_name, std::string_view root_mountpoint) -> bool {
        return gucc::services::enable_systemd_service(service_name, root_mountpoint);
    };

    services["disable"] = [](std::string_view service_name, std::string_view root_mountpoint) -> bool {
        return gucc::services::disable_systemd_service(service_name, root_mountpoint);
    };

    services["enable_user"] = [](std::string_view service_name, std::string_view root_mountpoint) -> bool {
        return gucc::services::enable_user_systemd_service(service_name, root_mountpoint);
    };

    services["unit_exists"] = [](std::string_view unit_name, std::string_view root_mountpoint) -> bool {
        return gucc::services::systemd_unit_exists(unit_name, root_mountpoint);
    };

    auto autologin = make_subtable(gucc_table, "autologin");

    autologin["enable"] = [](std::string_view displaymanager, std::string_view username, std::string_view root_mountpoint) -> bool {
        return gucc::user::enable_autologin(displaymanager, username, root_mountpoint);
    };
}

}  // namespace gucc::lua
