#include "config.hpp"
#include "definitions.hpp"  // for error_inter

#include <filesystem>  // for exists
#include <memory>      // for unique_ptr, make_unique, operator==

static std::unique_ptr<Config> s_config = nullptr;

namespace fs = std::filesystem;

bool Config::initialize() noexcept {
    if (s_config != nullptr) {
        error_inter("You should only initialize it once!\n");
        return false;
    }
    s_config = std::make_unique<Config>();
    if (s_config) {
        s_config->m_data["cachepath"]    = "/var/cache/pacman/pkg/";
        s_config->m_data["hostcache"]    = static_cast<std::int32_t>(!fs::exists("/run/miso/bootmnt"));
        s_config->m_data["menus"]        = 2;
        s_config->m_data["POST_INSTALL"] = "";

        s_config->m_data["H_INIT"] = "openrc";
        s_config->m_data["SYSTEM"] = "BIOS";
        s_config->m_data["KEYMAP"] = "us";
        s_config->m_data["XKBMAP"] = "us";

        // file systems
        s_config->m_data["BTRFS"]            = 0;
        s_config->m_data["ZFS"]              = 0;
        s_config->m_data["LUKS"]             = 0;
        s_config->m_data["LUKS_DEV"]         = "";
        s_config->m_data["LUKS_NAME"]        = "";
        s_config->m_data["LUKS_OPT"]         = "";  // Default or user-defined?
        s_config->m_data["LUKS_UUID"]        = "";
        s_config->m_data["LVM"]              = 0;
        s_config->m_data["LVM_LV_NAME"]      = "";  // Name of LV to create or use
        s_config->m_data["LVM_SEP_BOOT"]     = 0;
        s_config->m_data["READY_PARTITIONS"] = std::vector<std::string>{};
        s_config->m_data["ZFS_ZPOOL_NAMES"]  = std::vector<std::string>{};

        // URLs to fetch net profiles
        s_config->m_data["NET_PROFILES_URL"]          = "https://raw.githubusercontent.com/CachyOS/New-Cli-Installer/master/net-profiles.toml";
        s_config->m_data["NET_PROFILES_FALLBACK_URL"] = "file:///var/lib/cachyos-installer/net-profiles.toml";

        // Mounting
        s_config->m_data["MOUNTPOINT"] = "/mnt";

        // Installation
        s_config->m_data["HEADLESS_MODE"] = 0;
        s_config->m_data["SERVER_MODE"]   = 0;
        s_config->m_data["GRAPHIC_CARD"]  = "";
        s_config->m_data["DRIVERS_TYPE"]  = "free";
    }

    return s_config.get();
}

auto Config::instance() -> Config* {
    return s_config.get();
}
