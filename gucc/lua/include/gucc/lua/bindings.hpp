#ifndef GUCC_LUA_BINDINGS_HPP
#define GUCC_LUA_BINDINGS_HPP

#include <vector>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "gucc/partition.hpp"

namespace gucc::lua {

inline auto make_subtable(sol::table& parent, const char* name) -> sol::table {
    sol::state_view lua(parent.lua_state());
    auto t       = lua.create_table();
    parent[name] = t;
    return t;
}

template <typename T>
inline auto lua_get_or(const sol::table& t, const char* key, T default_val) -> T {
    sol::object v = t[key];
    return v.is<T>() ? v.as<T>() : std::move(default_val);
}

auto open_gucc_lib(sol::this_state lua) -> sol::table;

void register_pacmanconf(sol::table& gucc_table);
void register_initcpio(sol::table& gucc_table);
void register_fstab(sol::table& gucc_table);
void register_bootloader(sol::table& gucc_table);
void register_user(sol::table& gucc_table);
void register_locale(sol::table& gucc_table);
void register_partitioning(sol::table& gucc_table);
void register_kernel_params(sol::table& gucc_table);
void register_services(sol::table& gucc_table);
void register_mount(sol::table& gucc_table);
void register_file_utils(sol::table& gucc_table);
void register_package_profiles(sol::table& gucc_table);
void register_cpu(sol::table& gucc_table);

auto lua_partition_from_table(const sol::table& t) -> gucc::fs::Partition;
auto lua_partitions_from_table(const sol::table& t) -> std::vector<gucc::fs::Partition>;
auto lua_partition_to_table(sol::state_view& lua, const gucc::fs::Partition& p) -> sol::table;

}  // namespace gucc::lua

#endif  // GUCC_LUA_BINDINGS_HPP
