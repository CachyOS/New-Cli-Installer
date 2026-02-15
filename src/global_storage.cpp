#include "global_storage.hpp"
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
        s_config->m_data["NW_CMD"] = "";

        // Device and partition state
        s_config->m_data["DEVICE"]            = "";
        s_config->m_data["PARTITION"]         = "";
        s_config->m_data["ROOT_PART"]         = "";
        s_config->m_data["MOUNT"]             = "";
        s_config->m_data["MOUNT_OPTS"]        = "";
        s_config->m_data["INCLUDE_PART"]      = "";
        s_config->m_data["NUMBER_PARTITIONS"] = 0;
        s_config->m_data["PARTITIONS"]        = std::vector<std::string>{};
        s_config->m_data["FILESYSTEM_NAME"]   = "";
        s_config->m_data["FILESYSTEM"]        = "";

        // file systems
        s_config->m_data["BTRFS"]            = 0;
        s_config->m_data["ZFS"]              = 0;
        s_config->m_data["LUKS"]             = 0;
        s_config->m_data["LUKS_DEV"]         = "";
        s_config->m_data["LUKS_NAME"]        = "";
        s_config->m_data["LUKS_OPT"]         = "";  // Default or user-defined?
        s_config->m_data["LUKS_UUID"]        = "";
        s_config->m_data["LUKS_ROOT_NAME"]   = "";
        s_config->m_data["LVM"]              = 0;
        s_config->m_data["LVM_LV_NAME"]      = "";  // Name of LV to create or use
        s_config->m_data["LVM_SEP_BOOT"]     = 0;
        s_config->m_data["fde"]              = 0;
        s_config->m_data["READY_PARTITIONS"] = std::vector<std::string>{};
        s_config->m_data["PARTITION_SCHEMA"] = std::vector<gucc::fs::Partition>{};
        s_config->m_data["ZFS_ZPOOL_NAMES"]  = std::vector<std::string>{};
        s_config->m_data["ZFS_ZPOOL_NAME"]   = "zpcachyos";
        s_config->m_data["ZFS_SETUP_CONFIG"] = gucc::fs::ZfsSetupConfig{};

        // UEFI boot
        s_config->m_data["UEFI_MOUNT"] = "";
        s_config->m_data["UEFI_PART"]  = "";

        // used for swapon/swapoff
        s_config->m_data["SWAP_DEVICE"] = "";

        // URLs to fetch net profiles
        s_config->m_data["NET_PROFILES_URL"]          = "https://raw.githubusercontent.com/CachyOS/New-Cli-Installer/master/net-profiles.toml";
        s_config->m_data["NET_PROFILES_FALLBACK_URL"] = "file:///var/lib/cachyos-installer/net-profiles.toml";

        // Mounting
        s_config->m_data["MOUNTPOINT"] = "/mnt";
        s_config->m_data["BOOTLOADER"] = "";

        // Installation
        s_config->m_data["HEADLESS_MODE"] = 0;
        s_config->m_data["SERVER_MODE"]   = 0;
        s_config->m_data["GRAPHIC_CARD"]  = "";
        s_config->m_data["KERNEL"]        = "";
        s_config->m_data["DE"]            = "";

        // User configuration
        s_config->m_data["HOSTNAME"]   = "";
        s_config->m_data["LOCALE"]     = "";
        s_config->m_data["TIMEZONE"]   = "";
        s_config->m_data["USER_NAME"]  = "";
        s_config->m_data["USER_PASS"]  = "";
        s_config->m_data["USER_SHELL"] = "";
        s_config->m_data["ROOT_PASS"]  = "";
        s_config->m_data["PASSWD"]     = "";

        // Misc state
        s_config->m_data["CHECKLIST"] = "";
        s_config->m_data["FSCK_HOOK"] = 0;
        s_config->m_data["fs_opts"]   = "";
    }

    return s_config.get();
}

auto Config::instance() -> Config* {
    return s_config.get();
}
