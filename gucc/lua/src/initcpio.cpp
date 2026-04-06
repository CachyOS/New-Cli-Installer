#include "gucc/lua/bindings.hpp"

#include "gucc/initcpio.hpp"

namespace gucc::lua {

void register_initcpio(sol::table& gucc_table) {
    auto initcpio = make_subtable(gucc_table, "initcpio");

    initcpio["parse"] = [gucc_table](std::string_view file_path) -> sol::table {
        sol::state_view lua(gucc_table.lua_state());
        gucc::detail::Initcpio initcpio_obj{file_path};
        if (!initcpio_obj.parse_file()) {
            return sol::lua_nil;
        }
        auto result       = lua.create_table();
        result["modules"] = sol::as_table(initcpio_obj.modules);
        result["files"]   = sol::as_table(initcpio_obj.files);
        result["hooks"]   = sol::as_table(initcpio_obj.hooks);
        return result;
    };

    initcpio["write"] = [](std::string_view file_path, sol::table config) -> bool {
        gucc::detail::Initcpio initcpio_obj{file_path};
        if (!initcpio_obj.parse_file()) {
            return false;
        }
        if (sol::object modules = config["modules"]; modules.is<sol::table>()) {
            initcpio_obj.modules = modules.as<std::vector<std::string>>();
        }
        if (sol::object files = config["files"]; files.is<sol::table>()) {
            initcpio_obj.files = files.as<std::vector<std::string>>();
        }
        if (sol::object hooks = config["hooks"]; hooks.is<sol::table>()) {
            initcpio_obj.hooks = hooks.as<std::vector<std::string>>();
        }
        return initcpio_obj.write();
    };

    initcpio["append_module"] = [](std::string_view file_path, std::string module) -> bool {
        gucc::detail::Initcpio initcpio_obj{file_path};
        return initcpio_obj.append_module(std::move(module));
    };
    initcpio["append_hook"] = [](std::string_view file_path, std::string hook) -> bool {
        gucc::detail::Initcpio initcpio_obj{file_path};
        return initcpio_obj.append_hook(std::move(hook));
    };
    initcpio["append_file"] = [](std::string_view file_path, std::string file) -> bool {
        gucc::detail::Initcpio initcpio_obj{file_path};
        return initcpio_obj.append_file(std::move(file));
    };
    initcpio["remove_hook"] = [](std::string_view file_path, std::string hook) -> bool {
        gucc::detail::Initcpio initcpio_obj{file_path};
        return initcpio_obj.remove_hook(std::move(hook));
    };
    initcpio["remove_module"] = [](std::string_view file_path, std::string module) -> bool {
        gucc::detail::Initcpio initcpio_obj{file_path};
        return initcpio_obj.remove_module(std::move(module));
    };
    initcpio["insert_hook"] = [](std::string_view file_path, std::string needle, std::string hook) -> bool {
        gucc::detail::Initcpio initcpio_obj{file_path};
        return initcpio_obj.insert_hook(std::move(needle), std::move(hook));
    };

    initcpio["setup"] = [](std::string_view initcpio_path, sol::table config_table) -> bool {
        gucc::initcpio::InitcpioConfig config{};
        config.filesystem_type = gucc::fs::string_to_filesystem_type(
            config_table.get_or<std::string>("filesystem_type", "ext4"));
        config.is_lvm                = lua_get_or<bool>(config_table, "is_lvm", false);
        config.is_luks               = lua_get_or<bool>(config_table, "is_luks", false);
        config.has_swap              = lua_get_or<bool>(config_table, "has_swap", false);
        config.has_plymouth          = lua_get_or<bool>(config_table, "has_plymouth", false);
        config.use_systemd_hook      = lua_get_or<bool>(config_table, "use_systemd_hook", false);
        config.is_btrfs_multi_device = lua_get_or<bool>(config_table, "is_btrfs_multi_device", false);
        return gucc::initcpio::setup_initcpio_config(initcpio_path, config);
    };
}

}  // namespace gucc::lua
