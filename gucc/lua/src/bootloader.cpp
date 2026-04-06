#include "gucc/lua/bindings.hpp"

#include "gucc/bootloader.hpp"
#include "gucc/partition_config.hpp"

namespace gucc::lua {

void register_bootloader(sol::table& gucc_table) {
    auto bootloader = make_subtable(gucc_table, "bootloader");

    bootloader["from_string"] = [gucc_table](std::string_view name) -> sol::object {
        auto result = gucc::bootloader::bootloader_from_string(name);
        if (result) {
            return sol::make_object(gucc_table.lua_state(),
                std::string{gucc::bootloader::bootloader_to_string(*result)});
        }
        return sol::lua_nil;
    };

    bootloader["to_string"] = [gucc_table](std::string_view type_str) -> sol::object {
        auto type = gucc::bootloader::bootloader_from_string(type_str);
        if (type) {
            return sol::make_object(gucc_table.lua_state(),
                std::string{gucc::bootloader::bootloader_to_string(*type)});
        }
        return sol::lua_nil;
    };

    bootloader["gen_grub_config"] = [](sol::table cfg_table) -> std::string {
        gucc::bootloader::GrubConfig cfg{};
        cfg.default_entry         = cfg_table.get_or<std::string>("default_entry", "0");
        cfg.grub_timeout          = lua_get_or<std::int32_t>(cfg_table, "grub_timeout", 5);
        cfg.grub_distributor      = cfg_table.get_or<std::string>("grub_distributor", "Arch");
        cfg.cmdline_linux_default = cfg_table.get_or<std::string>("cmdline_linux_default", "loglevel=3 quiet");
        cfg.cmdline_linux         = cfg_table.get_or<std::string>("cmdline_linux", "");
        cfg.preload_modules       = cfg_table.get_or<std::string>("preload_modules", "part_gpt part_msdos");
        cfg.timeout_style         = cfg_table.get_or<std::string>("timeout_style", "menu");
        cfg.terminal_input        = cfg_table.get_or<std::string>("terminal_input", "console");
        cfg.gfxmode               = cfg_table.get_or<std::string>("gfxmode", "auto");
        cfg.gfxpayload_linux      = cfg_table.get_or<std::string>("gfxpayload_linux", "keep");
        cfg.disable_recovery      = lua_get_or<bool>(cfg_table, "disable_recovery", true);

        if (sol::object v = cfg_table["enable_cryptodisk"]; v.is<bool>()) {
            cfg.enable_cryptodisk = v.as<bool>();
        }
        if (sol::object v = cfg_table["terminal_output"]; v.is<std::string>()) {
            cfg.terminal_output = v.as<std::string>();
        }
        if (sol::object v = cfg_table["disable_linux_uuid"]; v.is<bool>()) {
            cfg.disable_linux_uuid = v.as<bool>();
        }
        if (sol::object v = cfg_table["color_normal"]; v.is<std::string>()) {
            cfg.color_normal = v.as<std::string>();
        }
        if (sol::object v = cfg_table["color_highlight"]; v.is<std::string>()) {
            cfg.color_highlight = v.as<std::string>();
        }
        if (sol::object v = cfg_table["background"]; v.is<std::string>()) {
            cfg.background = v.as<std::string>();
        }
        if (sol::object v = cfg_table["theme"]; v.is<std::string>()) {
            cfg.theme = v.as<std::string>();
        }
        if (sol::object v = cfg_table["init_tune"]; v.is<std::string>()) {
            cfg.init_tune = v.as<std::string>();
        }
        if (sol::object v = cfg_table["savedefault"]; v.is<bool>()) {
            cfg.savedefault = v.as<bool>();
        }
        if (sol::object v = cfg_table["disable_submenu"]; v.is<bool>()) {
            cfg.disable_submenu = v.as<bool>();
        }
        if (sol::object v = cfg_table["disable_os_prober"]; v.is<bool>()) {
            cfg.disable_os_prober = v.as<bool>();
        }
        return gucc::bootloader::gen_grub_config(cfg);
    };

    bootloader["write_grub_config"] = [](sol::table cfg_table, std::string_view root_mountpoint) -> bool {
        gucc::bootloader::GrubConfig cfg{};
        cfg.default_entry         = cfg_table.get_or<std::string>("default_entry", "0");
        cfg.grub_timeout          = lua_get_or<std::int32_t>(cfg_table, "grub_timeout", 5);
        cfg.grub_distributor      = cfg_table.get_or<std::string>("grub_distributor", "Arch");
        cfg.cmdline_linux_default = cfg_table.get_or<std::string>("cmdline_linux_default", "loglevel=3 quiet");
        cfg.cmdline_linux         = cfg_table.get_or<std::string>("cmdline_linux", "");
        cfg.preload_modules       = cfg_table.get_or<std::string>("preload_modules", "part_gpt part_msdos");
        cfg.timeout_style         = cfg_table.get_or<std::string>("timeout_style", "menu");
        cfg.terminal_input        = cfg_table.get_or<std::string>("terminal_input", "console");
        cfg.gfxmode               = cfg_table.get_or<std::string>("gfxmode", "auto");
        cfg.gfxpayload_linux      = cfg_table.get_or<std::string>("gfxpayload_linux", "keep");
        cfg.disable_recovery      = lua_get_or<bool>(cfg_table, "disable_recovery", true);
        if (sol::object v = cfg_table["enable_cryptodisk"]; v.is<bool>()) {
            cfg.enable_cryptodisk = v.as<bool>();
        }
        if (sol::object v = cfg_table["theme"]; v.is<std::string>()) {
            cfg.theme = v.as<std::string>();
        }
        if (sol::object v = cfg_table["savedefault"]; v.is<bool>()) {
            cfg.savedefault = v.as<bool>();
        }
        if (sol::object v = cfg_table["disable_submenu"]; v.is<bool>()) {
            cfg.disable_submenu = v.as<bool>();
        }
        return gucc::bootloader::write_grub_config(cfg, root_mountpoint);
    };

    bootloader["install_grub"] = [](sol::table cfg_table, sol::table install_table, std::string_view root_mountpoint) -> bool {
        gucc::bootloader::GrubConfig cfg{};
        cfg.default_entry         = cfg_table.get_or<std::string>("default_entry", "0");
        cfg.grub_timeout          = lua_get_or<std::int32_t>(cfg_table, "grub_timeout", 5);
        cfg.grub_distributor      = cfg_table.get_or<std::string>("grub_distributor", "Arch");
        cfg.cmdline_linux_default = cfg_table.get_or<std::string>("cmdline_linux_default", "loglevel=3 quiet");
        cfg.cmdline_linux         = cfg_table.get_or<std::string>("cmdline_linux", "");
        cfg.preload_modules       = cfg_table.get_or<std::string>("preload_modules", "part_gpt part_msdos");
        cfg.timeout_style         = cfg_table.get_or<std::string>("timeout_style", "menu");
        cfg.terminal_input        = cfg_table.get_or<std::string>("terminal_input", "console");
        cfg.gfxmode               = cfg_table.get_or<std::string>("gfxmode", "auto");
        cfg.gfxpayload_linux      = cfg_table.get_or<std::string>("gfxpayload_linux", "keep");
        cfg.disable_recovery      = lua_get_or<bool>(cfg_table, "disable_recovery", true);
        if (sol::object v = cfg_table["enable_cryptodisk"]; v.is<bool>()) {
            cfg.enable_cryptodisk = v.as<bool>();
        }

        gucc::bootloader::GrubInstallConfig install_cfg{};
        install_cfg.is_efi         = lua_get_or<bool>(install_table, "is_efi", true);
        install_cfg.do_recheck     = lua_get_or<bool>(install_table, "do_recheck", false);
        install_cfg.is_removable   = lua_get_or<bool>(install_table, "is_removable", false);
        install_cfg.is_root_on_zfs = lua_get_or<bool>(install_table, "is_root_on_zfs", false);
        if (sol::object v = install_table["device"]; v.is<std::string>()) {
            install_cfg.device = v.as<std::string>();
        }
        if (sol::object v = install_table["efi_directory"]; v.is<std::string>()) {
            install_cfg.efi_directory = v.as<std::string>();
        }
        if (sol::object v = install_table["bootloader_id"]; v.is<std::string>()) {
            install_cfg.bootloader_id = v.as<std::string>();
        }

        return gucc::bootloader::install_grub(cfg, install_cfg, root_mountpoint);
    };

    bootloader["install_systemd_boot"] = [](sol::table config_table) -> bool {
        std::string root_mountpoint = config_table.get_or<std::string>("root_mountpoint", "");
        std::string efi_directory   = config_table.get_or<std::string>("efi_directory", "/boot");
        bool is_removable           = false;
        if (sol::object v = config_table["is_removable"]; v.is<bool>()) {
            is_removable = v.as<bool>();
        }
        gucc::bootloader::SystemdBootInstallConfig config{
            .is_removable    = is_removable,
            .root_mountpoint = root_mountpoint,
            .efi_directory   = efi_directory,
        };
        return gucc::bootloader::install_systemd_boot(config);
    };

    bootloader["install_refind"] = [](sol::table config_table) -> bool {
        std::vector<std::string> extra_kerns;
        if (sol::object kernvers = config_table["extra_kernel_versions"]; kernvers.is<sol::table>()) {
            extra_kerns = kernvers.as<std::vector<std::string>>();
        }
        std::vector<std::string> kparams;
        if (sol::object params = config_table["kernel_params"]; params.is<sol::table>()) {
            kparams = params.as<std::vector<std::string>>();
        }
        std::string root_mountpoint = config_table.get_or<std::string>("root_mountpoint", "");
        std::string boot_mountpoint = config_table.get_or<std::string>("boot_mountpoint", "/boot");
        bool is_removable           = false;
        if (sol::object v = config_table["is_removable"]; v.is<bool>()) {
            is_removable = v.as<bool>();
        }
        gucc::bootloader::RefindInstallConfig config{
            .is_removable          = is_removable,
            .root_mountpoint       = root_mountpoint,
            .boot_mountpoint       = boot_mountpoint,
            .extra_kernel_versions = extra_kerns,
            .kernel_params         = kparams,
        };
        return gucc::bootloader::install_refind(config);
    };

    bootloader["install_limine"] = [](sol::table config_table) -> bool {
        std::vector<std::string> kparams;
        if (sol::object params = config_table["kernel_params"]; params.is<sol::table>()) {
            kparams = params.as<std::vector<std::string>>();
        }
        bool is_efi = true;
        if (sol::object v = config_table["is_efi"]; v.is<bool>()) {
            is_efi = v.as<bool>();
        }
        bool is_removable = false;
        if (sol::object v = config_table["is_removable"]; v.is<bool>()) {
            is_removable = v.as<bool>();
        }
        std::string root_mountpoint = config_table.get_or<std::string>("root_mountpoint", "");
        std::string boot_mountpoint = config_table.get_or<std::string>("boot_mountpoint", "/boot");
        gucc::bootloader::LimineInstallConfig config{
            .is_efi          = is_efi,
            .is_removable    = is_removable,
            .root_mountpoint = root_mountpoint,
            .boot_mountpoint = boot_mountpoint,
            .kernel_params   = kparams,
        };
        if (sol::object v = config_table["bios_device"]; v.is<std::string>()) {
            config.bios_device = v.as<std::string>();
        }
        if (sol::object v = config_table["efi_directory"]; v.is<std::string>()) {
            config.efi_directory = v.as<std::string>();
        }
        return gucc::bootloader::install_limine(config);
    };

    bootloader["gen_refind_config"] = [](sol::table params_table) -> std::string {
        auto kparams = params_table.as<std::vector<std::string>>();
        return gucc::bootloader::gen_refind_config(kparams);
    };

    bootloader["gen_limine_entry_config"] = [](sol::object params_obj, sol::object esp_path_obj) -> std::string {
        std::vector<std::string> kparams;
        if (params_obj.is<sol::table>()) {
            kparams = params_obj.as<std::vector<std::string>>();
        }
        std::optional<std::string> esp_path;
        if (esp_path_obj.is<std::string>()) {
            esp_path = esp_path_obj.as<std::string>();
        }
        return gucc::bootloader::gen_limine_entry_config(kparams, esp_path);
    };

    bootloader["refind_write_extra_kern_strings"] = [](std::string_view file_path, sol::table kerns_table) -> bool {
        auto kerns = kerns_table.as<std::vector<std::string>>();
        return gucc::bootloader::refind_write_extra_kern_strings(file_path, kerns);
    };

    bootloader["post_setup"] = [](sol::table config_table) -> bool {
        std::string root_mountpoint = config_table.get_or<std::string>("root_mountpoint", "");
        bool is_btrfs               = false;
        if (sol::object v = config_table["is_btrfs"]; v.is<bool>()) {
            is_btrfs = v.as<bool>();
        }
        auto bl_str  = config_table.get_or<std::string>("bootloader", "grub");
        auto bl_type = gucc::bootloader::bootloader_from_string(bl_str);
        gucc::bootloader::BootloaderPostSetupConfig config{
            .is_btrfs        = is_btrfs,
            .root_mountpoint = root_mountpoint,
            .bootloader      = bl_type.value_or(gucc::bootloader::BootloaderType::Grub),
        };
        return gucc::bootloader::bootloader_post_setup(config);
    };
}

}  // namespace gucc::lua
