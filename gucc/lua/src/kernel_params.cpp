#include "gucc/lua/bindings.hpp"

#include "gucc/kernel_params.hpp"

#include <utility>

namespace gucc::lua {

void register_kernel_params(sol::table& gucc_table) {
    auto kp = make_subtable(gucc_table, "kernel_params");

    kp["get"] = [gucc_table](sol::table partitions_table, std::string_view kernel_params_str, sol::object zfs_root_obj) -> sol::object {
        auto partitions = lua_partitions_from_table(partitions_table);

        std::optional<std::string> zfs_root;
        if (zfs_root_obj.is<std::string>()) {
            zfs_root = zfs_root_obj.as<std::string>();
        }

        auto result = gucc::fs::get_kernel_params(partitions, kernel_params_str, zfs_root);
        if (!result) {
            return sol::lua_nil;
        }

        return sol::make_object(gucc_table.lua_state(), sol::as_table(std::move(*result)));
    };
}

}  // namespace gucc::lua
