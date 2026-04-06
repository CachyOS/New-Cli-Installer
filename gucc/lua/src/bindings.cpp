#include "gucc/lua/bindings.hpp"

#include "gucc/partition.hpp"
#ifndef COS_BUILD_STATIC
#include "gucc/logger.hpp"
#endif

#include <spdlog/async.h>                     // for create_async
#include <spdlog/common.h>                    // for debug
#include <spdlog/sinks/stdout_color_sinks.h>  // for stdout_color_sink_mt
#include <spdlog/spdlog.h>                    // for set_default_logger, set_level

namespace gucc::lua {

auto open_gucc_lib(sol::this_state lua) -> sol::table {
    sol::state_view lua_state(lua);
    auto gucc_table = lua_state.create_table();

    register_pacmanconf(gucc_table);
    register_initcpio(gucc_table);
    register_fstab(gucc_table);
    register_bootloader(gucc_table);
    register_user(gucc_table);
    register_locale(gucc_table);
    register_partitioning(gucc_table);
    register_kernel_params(gucc_table);
    register_services(gucc_table);
    register_mount(gucc_table);
    register_file_utils(gucc_table);
    register_package_profiles(gucc_table);
    register_cpu(gucc_table);

    // Initialize logger.
    auto logger = spdlog::create_async<spdlog::sinks::stdout_color_sink_mt>("cachyos_logger");
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%r][%^---%L---%$] %v");
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_every(std::chrono::seconds(5));

#ifndef COS_BUILD_STATIC
    // Set gucc logger.
    gucc::logger::set_logger(logger);
#endif

    return gucc_table;
}

auto lua_partition_from_table(const sol::table& t) -> gucc::fs::Partition {
    gucc::fs::Partition p{};
    p.fstype     = t.get_or<std::string>("fstype", "");
    p.mountpoint = t.get_or<std::string>("mountpoint", "");
    p.uuid_str   = t.get_or<std::string>("uuid_str", "");
    p.device     = t.get_or<std::string>("device", "");
    p.size       = t.get_or<std::string>("size", "");
    p.mount_opts = t.get_or<std::string>("mount_opts", "");

    if (sol::object subvol = t["subvolume"]; subvol.is<std::string>()) {
        p.subvolume = subvol.as<std::string>();
    }
    if (sol::object luks_mapper = t["luks_mapper_name"]; luks_mapper.is<std::string>()) {
        p.luks_mapper_name = luks_mapper.as<std::string>();
    }
    if (sol::object luks_uuid = t["luks_uuid"]; luks_uuid.is<std::string>()) {
        p.luks_uuid = luks_uuid.as<std::string>();
    }
    if (sol::object luks_pass = t["luks_passphrase"]; luks_pass.is<std::string>()) {
        p.luks_passphrase = luks_pass.as<std::string>();
    }
    return p;
}

auto lua_partitions_from_table(const sol::table& t) -> std::vector<gucc::fs::Partition> {
    std::vector<gucc::fs::Partition> partitions;
    t.for_each([&](sol::object, sol::object value) {
        if (value.is<sol::table>()) {
            partitions.push_back(lua_partition_from_table(value.as<sol::table>()));
        }
    });
    return partitions;
}

auto lua_partition_to_table(sol::state_view& lua, const gucc::fs::Partition& p) -> sol::table {
    auto t          = lua.create_table();
    t["fstype"]     = p.fstype;
    t["mountpoint"] = p.mountpoint;
    t["uuid_str"]   = p.uuid_str;
    t["device"]     = p.device;
    t["size"]       = p.size;
    t["mount_opts"] = p.mount_opts;
    if (p.subvolume) {
        t["subvolume"] = *p.subvolume;
    }
    if (p.luks_mapper_name) {
        t["luks_mapper_name"] = *p.luks_mapper_name;
    }
    if (p.luks_uuid) {
        t["luks_uuid"] = *p.luks_uuid;
    }
    if (p.luks_passphrase) {
        t["luks_passphrase"] = *p.luks_passphrase;
    }
    return t;
}

}  // namespace gucc::lua

extern "C" int luaopen_gucc(lua_State* L) {
    return sol::stack::call_lua(L, 1, gucc::lua::open_gucc_lib);
}
