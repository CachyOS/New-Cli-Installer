#!/usr/bin/env lua
-- Full CachyOS Installation Pipeline using GUCC Lua API
-- Replaces: shellprocess*.conf, initcpiocfg, fstab, grubcfg,
--           hwclock, localecfg, services-systemd, bootloader (config parts)
--
-- This script demonstrates how the entire Calamares installation pipeline
-- can be rewritten as a Lua script using the GUCC library.
--
-- Usage: sudo lua calamares_full_install.lua
-- Expected env vars:
--   ROOT_MOUNTPOINT  - Target root (e.g., /mnt)
--   BOOTLOADER       - Bootloader type (grub, systemd-boot, limine, refind)
--   HOSTNAME         - System hostname
--   TIMEZONE         - Timezone (e.g., America/New_York)
--   LOCALE           - Locale (e.g., en_US.UTF-8 UTF-8)
--   KEYMAP           - Console keymap (e.g., us)
--   USERNAME         - Primary user name
--   USER_PASSWORD    - User password
--   ROOT_PASSWORD    - Root password
--   USER_SHELL       - User shell (e.g., /bin/bash)

local gucc = require "gucc"

local ROOT = os.getenv("ROOT_MOUNTPOINT") or "/mnt"
local BOOTLOADER = os.getenv("BOOTLOADER") or "grub"
local HOSTNAME = os.getenv("HOSTNAME") or "cachyos"
local TIMEZONE = os.getenv("TIMEZONE") or "America/New_York"
local LOCALE = os.getenv("LOCALE") or "en_US.UTF-8 UTF-8"
local KEYMAP = os.getenv("KEYMAP") or "us"
local USERNAME = os.getenv("USERNAME") or "user"
local USER_PASSWORD = os.getenv("USER_PASSWORD") or "user"
local ROOT_PASSWORD = os.getenv("ROOT_PASSWORD") or "root"
local USER_SHELL = os.getenv("USER_SHELL") or "/bin/bash"

local function phase(name)
    print("\n=== " .. name .. " ===")
end

local function check(ok, msg)
    if not ok then
        print("ERROR: " .. (msg or "operation failed"))
    end
end

---------------------------------------------------------------
-- Phase 1: Generate fstab and crypttab
-- Replaces: fstab/main.py (436 lines of Python)
---------------------------------------------------------------
phase("Phase 1: Generate fstab/crypttab")
-- In a real scenario, partition data comes from the partitioning module.
-- Here we show the API usage with example data:
--
-- local partitions = {
--     {fstype="vfat", mountpoint="/boot", uuid_str="AAAA-BBBB",
--      device="/dev/nvme0n1p1", mount_opts="umask=0077"},
--     {fstype="btrfs", mountpoint="/", uuid_str="CCCC-DDDD",
--      device="/dev/nvme0n1p2", mount_opts="compress=zstd:1", subvolume="@"},
-- }
-- check(gucc.fstab.generate(partitions, ROOT), "fstab generation failed")
-- check(gucc.crypttab.generate(partitions, ROOT, "luks"), "crypttab generation failed")
--
-- Alternative: use genfstab if partition schema is unknown:
check(gucc.fstab.run_genfstab(ROOT), "genfstab failed")

---------------------------------------------------------------
-- Phase 2: Configure mkinitcpio
-- Replaces: initcpiocfg/main.py (239 lines of Python)
---------------------------------------------------------------
phase("Phase 2: Configure mkinitcpio")
check(gucc.initcpio.setup(ROOT .. "/etc/mkinitcpio.conf", {
    filesystem_type = "btrfs",
    is_lvm = false,
    is_luks = false,
    has_swap = true,
    has_plymouth = true,
    use_systemd_hook = true,
}), "initcpio setup failed")

---------------------------------------------------------------
-- Phase 3: Set timezone and hwclock
-- Replaces: hwclock/main.py (56 lines of Python)
---------------------------------------------------------------
phase("Phase 3: Timezone and hwclock")
check(gucc.timezone.set(TIMEZONE, ROOT), "timezone failed")
check(gucc.hwclock.set_utc(ROOT), "hwclock failed")

---------------------------------------------------------------
-- Phase 4: Configure locale
-- Replaces: localecfg/main.py (175 lines of Python)
---------------------------------------------------------------
phase("Phase 4: Locale configuration")
check(gucc.locale.set_locale(LOCALE, ROOT), "locale failed")
check(gucc.locale.set_keymap(KEYMAP, ROOT), "keymap failed")

---------------------------------------------------------------
-- Phase 5: Configure bootloader
-- Replaces: bootloader/main.py (1150 lines) + grubcfg/main.py (335 lines)
---------------------------------------------------------------
phase("Phase 5: Bootloader configuration (" .. BOOTLOADER .. ")")

if BOOTLOADER == "grub" then
    local grub_cfg = gucc.bootloader.gen_grub_config({
        grub_timeout = 5,
        grub_distributor = "CachyOS",
        cmdline_linux_default = "quiet nowatchdog",
        enable_cryptodisk = false,
        disable_recovery = true,
        disable_submenu = true,
        theme = "/usr/share/grub/themes/cachyos/theme.txt",
        savedefault = false,
    })
    check(gucc.bootloader.write_grub_config({
        grub_timeout = 5,
        grub_distributor = "CachyOS",
        cmdline_linux_default = "quiet nowatchdog",
        theme = "/usr/share/grub/themes/cachyos/theme.txt",
    }, ROOT), "grub config write failed")
    check(gucc.bootloader.install_grub({
        grub_timeout = 5,
        grub_distributor = "CachyOS",
        cmdline_linux_default = "quiet nowatchdog",
    }, {
        is_efi = true,
        do_recheck = true,
        efi_directory = "/boot",
        bootloader_id = "cachyos",
    }, ROOT), "grub install failed")

elseif BOOTLOADER == "systemd-boot" then
    check(gucc.bootloader.install_systemd_boot({
        is_removable = false,
        root_mountpoint = ROOT,
        efi_directory = "/boot",
    }), "systemd-boot install failed")

elseif BOOTLOADER == "limine" then
    check(gucc.bootloader.install_limine({
        is_efi = true,
        root_mountpoint = ROOT,
        boot_mountpoint = "/boot",
        kernel_params = {"quiet", "nowatchdog"},
    }), "limine install failed")

    check(gucc.bootloader.post_setup({
        is_btrfs = true,
        root_mountpoint = ROOT,
        bootloader = "limine",
    }), "limine post-setup failed")

elseif BOOTLOADER == "refind" then
    check(gucc.bootloader.install_refind({
        is_removable = false,
        root_mountpoint = ROOT,
        boot_mountpoint = "/boot",
        extra_kernel_versions = {"linux-cachyos-bore", "linux-cachyos-lts", "linux-cachyos"},
        kernel_params = {"quiet", "nowatchdog"},
    }), "refind install failed")
end

---------------------------------------------------------------
-- Phase 6: User and hostname setup
-- Replaces: users module
---------------------------------------------------------------
phase("Phase 6: User setup")
check(gucc.user.set_hostname(HOSTNAME, ROOT), "hostname failed")
check(gucc.user.set_hosts(HOSTNAME, ROOT), "hosts failed")
check(gucc.user.set_root_password(ROOT_PASSWORD, ROOT), "root password failed")
check(gucc.user.create_new_user(
    {username = USERNAME, password = USER_PASSWORD, shell = USER_SHELL, sudoers_group = "wheel"},
    {"wheel", "audio", "video", "storage", "network", "rfkill", "sys", "lp", "scanner", "optical"},
    ROOT
), "user creation failed")

---------------------------------------------------------------
-- Phase 7: Enable systemd services
-- Replaces: services-systemd/main.py (91 lines) + shellprocess_enable_ufw.conf
---------------------------------------------------------------
phase("Phase 7: Enable services")
local services = {
    "NetworkManager",
    "bluetooth",
    "cups",
    "avahi-daemon",
    "systemd-timesyncd",
    "fstrim.timer",
}
for _, svc in ipairs(services) do
    check(gucc.services.enable(svc, ROOT), "enable " .. svc .. " failed")
end
check(gucc.services.enable("ufw", ROOT), "ufw enable failed")

---------------------------------------------------------------
-- Phase 8: Post-install
---------------------------------------------------------------
phase("Phase 8: Post-install cleanup")
-- Copy skel to user home (equivalent to shellprocess.conf step)
-- gucc.file_utils operations on ROOT .. "/home/" .. USERNAME

print("\n=== Installation Complete ===")
