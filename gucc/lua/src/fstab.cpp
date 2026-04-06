#include "gucc/lua/bindings.hpp"

#include "gucc/crypttab.hpp"
#include "gucc/fstab.hpp"

namespace gucc::lua {

void register_fstab(sol::table& gucc_table) {
    auto fstab = make_subtable(gucc_table, "fstab");

    fstab["generate"] = [](sol::table partitions_table, std::string_view root_mountpoint) -> bool {
        auto partitions = lua_partitions_from_table(partitions_table);
        return gucc::fs::generate_fstab(partitions, root_mountpoint);
    };

    fstab["generate_content"] = [](sol::table partitions_table) -> std::string {
        auto partitions = lua_partitions_from_table(partitions_table);
        return gucc::fs::generate_fstab_content(partitions);
    };

    fstab["run_genfstab"] = [](std::string_view root_mountpoint) -> bool {
        return gucc::fs::run_genfstab_on_mount(root_mountpoint);
    };

    auto crypttab = make_subtable(gucc_table, "crypttab");

    crypttab["generate"] = [](sol::table partitions_table, std::string_view root_mountpoint, std::string_view crypttab_opts) -> bool {
        auto partitions = lua_partitions_from_table(partitions_table);
        return gucc::fs::generate_crypttab(partitions, root_mountpoint, crypttab_opts);
    };

    crypttab["generate_content"] = [](sol::table partitions_table, std::string_view crypttab_opts) -> std::string {
        auto partitions = lua_partitions_from_table(partitions_table);
        return gucc::fs::generate_crypttab_content(partitions, crypttab_opts);
    };
}

}  // namespace gucc::lua
