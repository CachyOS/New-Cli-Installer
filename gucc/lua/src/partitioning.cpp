#include "gucc/lua/bindings.hpp"

#include "gucc/partition_config.hpp"
#include "gucc/partitioning.hpp"

namespace {
auto partitions_to_table(sol::state_view& lua, const std::vector<gucc::fs::Partition>& partitions) -> sol::table {
    auto result = lua.create_table();
    for (size_t i = 0; i < partitions.size(); ++i) {
        result[i + 1] = gucc::lua::lua_partition_to_table(lua, partitions[i]);
    }
    return result;
}
}  // namespace

namespace gucc::lua {

void register_partitioning(sol::table& gucc_table) {
    auto part_cfg = make_subtable(gucc_table, "partition_config");

    part_cfg["fs_type_to_string"] = [](std::string_view fs_name) -> std::string {
        auto fs_type = gucc::fs::string_to_filesystem_type(fs_name);
        return std::string{gucc::fs::filesystem_type_to_string(fs_type)};
    };
    part_cfg["string_to_fs_type"] = [](std::string_view fs_name) -> std::string {
        auto fs_type = gucc::fs::string_to_filesystem_type(fs_name);
        return std::string{gucc::fs::filesystem_type_to_string(fs_type)};
    };
    part_cfg["get_default_mount_opts"] = [](std::string_view fs_name, sol::object is_ssd_obj) -> std::string {
        auto fs_type = gucc::fs::string_to_filesystem_type(fs_name);
        bool is_ssd  = false;
        if (is_ssd_obj.is<bool>()) {
            is_ssd = is_ssd_obj.as<bool>();
        }
        return gucc::fs::get_default_mount_opts(fs_type, is_ssd);
    };
    part_cfg["get_mkfs_command"] = [](std::string_view fs_name) -> std::string {
        auto fs_type = gucc::fs::string_to_filesystem_type(fs_name);
        return std::string{gucc::fs::get_mkfs_command(fs_type)};
    };
    part_cfg["get_fstab_fs_name"] = [](std::string_view fs_name) -> std::string {
        auto fs_type = gucc::fs::string_to_filesystem_type(fs_name);
        return std::string{gucc::fs::get_fstab_fs_name(fs_type)};
    };
    part_cfg["get_sfdisk_type_alias"] = [](std::string_view fs_name) -> std::string {
        auto fs_type = gucc::fs::string_to_filesystem_type(fs_name);
        return std::string{gucc::fs::get_sfdisk_type_alias(fs_type)};
    };
    part_cfg["get_available_mount_opts"] = [gucc_table](std::string_view fs_name) -> sol::table {
        sol::state_view lua(gucc_table.lua_state());
        auto fs_type = gucc::fs::string_to_filesystem_type(fs_name);
        auto opts    = gucc::fs::get_available_mount_opts(fs_type);
        auto result  = lua.create_table();
        for (size_t i = 0; i < opts.size(); ++i) {
            auto entry           = lua.create_table();
            entry["name"]        = opts[i].name;
            entry["description"] = opts[i].description;
            result[i + 1]        = entry;
        }
        return result;
    };

    auto partitioning = make_subtable(gucc_table, "partitioning");

    partitioning["generate_default_schema"] = [gucc_table](std::string_view device, std::string_view boot_mountpoint, sol::object is_efi_obj) -> sol::table {
        sol::state_view lua(gucc_table.lua_state());
        bool is_efi = true;
        if (is_efi_obj.is<bool>()) {
            is_efi = is_efi_obj.as<bool>();
        }
        auto partitions = gucc::disk::generate_default_partition_schema(device, boot_mountpoint, is_efi);
        return partitions_to_table(lua, partitions);
    };

    partitioning["generate_schema_from_config"] = [gucc_table](std::string_view device, sol::table config_table, sol::object is_efi_obj) -> sol::table {
        sol::state_view lua(gucc_table.lua_state());
        gucc::fs::DefaultPartitionSchemaConfig config{};
        config.root_fs_type = gucc::fs::string_to_filesystem_type(
            config_table.get_or<std::string>("root_fs_type", "btrfs"));
        config.efi_partition_size      = config_table.get_or<std::string>("efi_partition_size", "2GiB");
        config.is_ssd                  = lua_get_or<bool>(config_table, "is_ssd", false);
        config.boot_mountpoint         = config_table.get_or<std::string>("boot_mountpoint", "/boot");
        config.create_btrfs_subvolumes = lua_get_or<bool>(config_table, "create_btrfs_subvolumes", true);

        if (sol::object v = config_table["swap_partition_size"]; v.is<std::string>()) {
            config.swap_partition_size = v.as<std::string>();
        }
        if (sol::object v = config_table["boot_partition_size"]; v.is<std::string>()) {
            config.boot_partition_size = v.as<std::string>();
        }
        if (sol::object v = config_table["root_mount_opts"]; v.is<std::string>()) {
            config.root_mount_opts = v.as<std::string>();
        }

        bool is_efi = true;
        if (is_efi_obj.is<bool>()) {
            is_efi = is_efi_obj.as<bool>();
        }
        auto partitions = gucc::disk::generate_partition_schema_from_config(device, config, is_efi);
        return partitions_to_table(lua, partitions);
    };

    partitioning["validate_schema"] = [gucc_table](sol::table partitions_table, std::string_view device, sol::object is_efi_obj) -> sol::table {
        sol::state_view lua(gucc_table.lua_state());
        auto partitions = lua_partitions_from_table(partitions_table);
        bool is_efi     = true;
        if (is_efi_obj.is<bool>()) {
            is_efi = is_efi_obj.as<bool>();
        }
        auto validation    = gucc::disk::validate_partition_schema(partitions, device, is_efi);
        auto result        = lua.create_table();
        result["is_valid"] = validation.is_valid;
        auto errors        = lua.create_table();
        for (size_t i = 0; i < validation.errors.size(); ++i) {
            errors[i + 1] = validation.errors[i];
        }
        result["errors"] = errors;
        auto warnings    = lua.create_table();
        for (size_t i = 0; i < validation.warnings.size(); ++i) {
            warnings[i + 1] = validation.warnings[i];
        }
        result["warnings"] = warnings;
        return result;
    };

    partitioning["preview_schema"] = [](sol::table partitions_table, std::string_view device, sol::object is_efi_obj) -> std::string {
        auto partitions = lua_partitions_from_table(partitions_table);
        bool is_efi     = true;
        if (is_efi_obj.is<bool>()) {
            is_efi = is_efi_obj.as<bool>();
        }
        return gucc::disk::preview_partition_schema(partitions, device, is_efi);
    };

    partitioning["gen_sfdisk_command"] = [](sol::table partitions_table, sol::object is_efi_obj) -> std::string {
        auto partitions = lua_partitions_from_table(partitions_table);
        bool is_efi     = true;
        if (is_efi_obj.is<bool>()) {
            is_efi = is_efi_obj.as<bool>();
        }
        return gucc::disk::gen_sfdisk_command(partitions, is_efi);
    };

    partitioning["apply_schema"] = [](std::string_view device, sol::table partitions_table, sol::object is_efi_obj, sol::object erase_first_obj) -> bool {
        auto partitions = lua_partitions_from_table(partitions_table);
        bool is_efi     = true;
        if (is_efi_obj.is<bool>()) {
            is_efi = is_efi_obj.as<bool>();
        }
        bool erase_first = true;
        if (erase_first_obj.is<bool>()) {
            erase_first = erase_first_obj.as<bool>();
        }
        return gucc::disk::apply_partition_schema(device, partitions, is_efi, erase_first);
    };

    partitioning["erase_disk"] = [](std::string_view device) -> bool {
        return gucc::disk::erase_disk(device);
    };
}

}  // namespace gucc::lua
