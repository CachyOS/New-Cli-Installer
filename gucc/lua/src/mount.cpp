#include "gucc/lua/bindings.hpp"

#include "gucc/mount_partitions.hpp"
#include "gucc/umount_partitions.hpp"

namespace gucc::lua {

void register_mount(sol::table& gucc_table) {
    auto mount = make_subtable(gucc_table, "mount");

    mount["mount_partition"] = [](std::string_view partition, std::string_view mount_dir, sol::object mount_opts_obj) -> bool {
        std::string mount_opts = "defaults";
        if (mount_opts_obj.is<std::string>()) {
            mount_opts = mount_opts_obj.as<std::string>();
        }
        return gucc::mount::mount_partition(partition, mount_dir, mount_opts);
    };

    mount["query_partition"] = [gucc_table](std::string_view partition) -> sol::table {
        sol::state_view lua(gucc_table.lua_state());
        std::int32_t is_luks{0};
        std::int32_t is_lvm{0};
        std::string luks_name{};
        std::string luks_dev{};
        std::string luks_uuid{};
        auto ok             = gucc::mount::query_partition(partition, is_luks, is_lvm, luks_name, luks_dev, luks_uuid);
        auto result         = lua.create_table();
        result["ok"]        = ok;
        result["is_luks"]   = is_luks;
        result["is_lvm"]    = is_lvm;
        result["luks_name"] = luks_name;
        result["luks_dev"]  = luks_dev;
        result["luks_uuid"] = luks_uuid;
        return result;
    };

    mount["setup_esp_partition"] = [gucc_table](std::string_view device, std::string_view mountpoint, std::string_view base_mountpoint, sol::object format_obj, sol::object is_ssd_obj) -> sol::object {
        sol::state_view lua(gucc_table.lua_state());
        bool format = false;
        if (format_obj.is<bool>()) {
            format = format_obj.as<bool>();
        }
        bool is_ssd = false;
        if (is_ssd_obj.is<bool>()) {
            is_ssd = is_ssd_obj.as<bool>();
        }
        auto result = gucc::mount::setup_esp_partition(device, mountpoint, base_mountpoint, format, is_ssd);
        if (!result) {
            return sol::lua_nil;
        }
        return lua_partition_to_table(lua, *result);
    };

    auto umount = make_subtable(gucc_table, "umount");

    umount["umount_partitions"] = [](std::string_view root_mountpoint, sol::object zfs_pools_obj) -> bool {
        std::vector<std::string> zfs_poolnames;
        if (zfs_pools_obj.is<sol::table>()) {
            zfs_poolnames = zfs_pools_obj.as<std::vector<std::string>>();
        }
        return gucc::umount::umount_partitions(root_mountpoint, zfs_poolnames);
    };
}

}  // namespace gucc::lua
