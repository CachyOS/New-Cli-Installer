# GUCC Lua Bindings

Lua bindings for the GUCC C++ library

## Quick Start

### Build

```bash
# Meson
meson setup build --buildtype=release -Dbuild_lua_bindings=true
ninja -C build

# CMake
cmake -B build -DCOS_BUILD_LUA_BINDINGS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Dependencies:** sol2 (v3.3.1+, fetched via meson wrap), Lua 5.3+/5.4/LuaJIT

### Usage

```lua
local gucc = require "gucc"
```

## API Reference

### gucc.pacmanconf

| Function | Signature | Returns |
|-|-|-|
| `push_repos_front` | `(file_path: string, value: string)` | `bool` |
| `get_repo_list` | `(file_path: string)` | `table` |

### gucc.repos

| Function | Signature | Returns |
|-|-|-|
| `install_cachyos_repos` | `()` | `bool` |

### gucc.initcpio

| Function | Signature | Returns |
|-|-|-|
| `parse` | `(file_path: string)` | `table\|nil` — `{modules={}, files={}, hooks={}}` |
| `write` | `(file_path: string, config: table)` | `bool` |
| `setup` | `(initcpio_path: string, config: table)` | `bool` |
| `append_module` | `(file_path: string, module: string)` | `bool` |
| `append_hook` | `(file_path: string, hook: string)` | `bool` |
| `append_file` | `(file_path: string, file: string)` | `bool` |
| `remove_hook` | `(file_path: string, hook: string)` | `bool` |
| `remove_module` | `(file_path: string, module: string)` | `bool` |
| `insert_hook` | `(file_path: string, needle: string, hook: string)` | `bool` |

**`setup` config table:**
```lua
{
    filesystem_type = "btrfs",    -- "btrfs", "ext4", "xfs", "f2fs", "zfs", "vfat"
    is_lvm = false,
    is_luks = false,
    has_swap = true,
    has_plymouth = true,
    use_systemd_hook = true,
    is_btrfs_multi_device = false,
}
```

### gucc.fstab

| Function | Signature | Returns |
|-|-|-|
| `generate` | `(partitions: table, root_mountpoint: string)` | `bool` |
| `generate_content` | `(partitions: table)` | `string` |
| `run_genfstab` | `(root_mountpoint: string)` | `bool` |

### gucc.crypttab

| Function | Signature | Returns |
|-|-|-|
| `generate` | `(partitions: table, root_mountpoint: string, crypttab_opts: string)` | `bool` |
| `generate_content` | `(partitions: table, crypttab_opts: string)` | `string` |

### gucc.bootloader

| Function | Signature | Returns |
|-|-|-|
| `from_string` | `(name: string)` | `string\|nil` — "grub", "systemd-boot", "refind", "limine" |
| `to_string` | `(type_str: string)` | `string\|nil` |
| `gen_grub_config` | `(config: table)` | `string` |
| `write_grub_config` | `(config: table, root_mountpoint: string)` | `bool` |
| `install_grub` | `(config: table, install_config: table, root_mountpoint: string)` | `bool` |
| `install_systemd_boot` | `(config: table)` | `bool` |
| `install_refind` | `(config: table)` | `bool` |
| `install_limine` | `(config: table)` | `bool` |
| `gen_refind_config` | `(kernel_params: table)` | `string` |
| `gen_limine_entry_config` | `(kernel_params: table, esp_path: string?)` | `string` |
| `refind_write_extra_kern_strings` | `(file_path: string, kernel_versions: table)` | `bool` |
| `post_setup` | `(config: table)` | `bool` |

**GrubConfig table:**
```lua
{
    default_entry = "0",
    grub_timeout = 5,
    grub_distributor = "CachyOS",
    cmdline_linux_default = "quiet nowatchdog",
    cmdline_linux = "",
    preload_modules = "part_gpt part_msdos",
    enable_cryptodisk = true,         -- optional bool
    timeout_style = "menu",
    terminal_input = "console",
    terminal_output = "...",          -- optional string
    gfxmode = "auto",
    gfxpayload_linux = "keep",
    disable_linux_uuid = false,       -- optional bool
    disable_recovery = true,
    color_normal = "...",             -- optional string
    color_highlight = "...",          -- optional string
    background = "...",               -- optional string
    theme = "/usr/share/grub/themes/cachyos/theme.txt",  -- optional string
    init_tune = "...",                -- optional string
    savedefault = false,              -- optional bool
    disable_submenu = true,           -- optional bool
    disable_os_prober = false,        -- optional bool
}
```

**GrubInstallConfig table:**
```lua
{
    is_efi = true,
    do_recheck = false,
    is_removable = false,
    is_root_on_zfs = false,
    device = "/dev/sda",           -- optional
    efi_directory = "/boot",       -- optional
    bootloader_id = "cachyos",     -- optional
}
```

### gucc.user

| Function | Signature | Returns |
|-|-|-|
| `set_hostname` | `(hostname: string, mountpoint: string)` | `bool` |
| `set_hosts` | `(hostname: string, mountpoint: string)` | `bool` |
| `set_root_password` | `(password: string, mountpoint: string)` | `bool` |
| `create_new_user` | `(user_info: table, groups: table, mountpoint: string)` | `bool` |
| `set_user_password` | `(username: string, password: string, mountpoint: string)` | `bool` |
| `create_group` | `(group: string, mountpoint: string, is_system: bool?)` | `bool` |
| `setup_user_environment` | `(info: table, mountpoint: string)` | `bool` |
| `enable_autologin` | `(displaymanager: string, username: string, mountpoint: string)` | `bool` |

**UserInfo table:**
```lua
{
    username = "user",
    password = "secret",
    shell = "/bin/bash",
    sudoers_group = "wheel",
}
```

### gucc.locale

| Function | Signature | Returns |
|-|-|-|
| `set_locale` | `(locale: string, mountpoint: string)` | `bool` |
| `prepare_locale_set` | `(locale: string, mountpoint: string)` | `bool` |
| `get_possible_locales` | `()` | `table` |
| `set_xkbmap` | `(xkbmap: string, mountpoint: string)` | `bool` |
| `get_x11_keymap_layouts` | `()` | `table` |
| `parse_locale_gen` | `(content: string)` | `table` |
| `set_keymap` | `(keymap: string, mountpoint: string)` | `bool` |
| `get_vconsole_keymap_list` | `()` | `table` |

### gucc.timezone

| Function | Signature | Returns |
|-|-|-|
| `set` | `(timezone: string, mountpoint: string)` | `bool` |
| `get_available` | `()` | `table` |
| `get_regions` | `()` | `table` |
| `get_zones` | `(region: string)` | `table` |

### gucc.hwclock

| Function | Signature | Returns |
|-|-|-|
| `set_utc` | `(root_mountpoint: string)` | `bool` |
| `set_localtime` | `(root_mountpoint: string)` | `bool` |

### gucc.partition_config

| Function | Signature | Returns |
|-|-|-|
| `fs_type_to_string` | `(fs_name: string)` | `string` |
| `string_to_fs_type` | `(fs_name: string)` | `string` |
| `get_default_mount_opts` | `(fs_name: string, is_ssd: bool?)` | `string` |
| `get_mkfs_command` | `(fs_name: string)` | `string` |
| `get_fstab_fs_name` | `(fs_name: string)` | `string` |
| `get_sfdisk_type_alias` | `(fs_name: string)` | `string` |
| `get_available_mount_opts` | `(fs_name: string)` | `table` |

Filesystem type strings: `"btrfs"`, `"ext4"`, `"f2fs"`, `"xfs"`, `"vfat"`, `"linuxswap"`, `"zfs"`

### gucc.partitioning

| Function | Signature | Returns |
|-|-|-|
| `generate_default_schema` | `(device: string, boot_mountpoint: string, is_efi: bool?)` | `table` |
| `generate_schema_from_config` | `(device: string, config: table, is_efi: bool?)` | `table` |
| `validate_schema` | `(partitions: table, device: string, is_efi: bool?)` | `table` |
| `preview_schema` | `(partitions: table, device: string, is_efi: bool?)` | `string` |
| `gen_sfdisk_command` | `(partitions: table, is_efi: bool?)` | `string` |
| `apply_schema` | `(device: string, partitions: table, is_efi: bool?, erase_first: bool?)` | `bool` |
| `erase_disk` | `(device: string)` | `bool` |

**Validation result table:**
```lua
{
    is_valid = true,
    errors = {"error1", "error2"},
    warnings = {"warning1"},
}
```

### gucc.kernel_params

| Function | Signature | Returns |
|-|-|-|
| `get` | `(partitions: table?, kernel_params: string, zfs_root: string?)` | `table\|nil` |

Returns table of kernel parameter strings, or `nil` on failure.

### gucc.services

| Function | Signature | Returns |
|-|-|-|
| `enable` | `(service_name: string, root_mountpoint: string)` | `bool` |
| `disable` | `(service_name: string, root_mountpoint: string)` | `bool` |
| `enable_user` | `(service_name: string, root_mountpoint: string)` | `bool` |
| `unit_exists` | `(unit_name: string, root_mountpoint: string)` | `bool` |

### gucc.autologin

| Function | Signature | Returns |
|-|-|-|
| `enable` | `(displaymanager: string, username: string, root_mountpoint: string)` | `bool` |

Supported display managers: `"sddm"`, `"gdm"`, `"lightdm"`, `"lxdm"`, `"greetd"`

### gucc.mount

| Function | Signature | Returns |
|-|-|-|
| `mount_partition` | `(partition: string, mount_dir: string, mount_opts: string?)` | `bool` |
| `query_partition` | `(partition: string)` | `table` |
| `setup_esp_partition` | `(device: string, mountpoint: string, base_mountpoint: string, format: bool?, is_ssd: bool?)` | `table\|nil` |

### gucc.umount

| Function | Signature | Returns |
|-|-|-|
| `umount_partitions` | `(root_mountpoint: string, zfs_pools: table?)` | `bool` |

### gucc.file_utils

| Function | Signature | Returns |
|-|-|-|
| `read_whole_file` | `(filepath: string)` | `string` |
| `write_to_file` | `(data: string, filepath: string)` | `bool` |
| `create_file_for_overwrite` | `(filepath: string, data: string)` | `bool` |

### gucc.string_utils

| Function | Signature | Returns |
|-|-|-|
| `make_multiline` | `(str: string, reverse: bool?, delim: string?)` | `table` |
| `join` | `(lines: table, delim: string?)` | `string` |
| `trim` | `(str: string)` | `string` |
| `ltrim` | `(str: string)` | `string` |
| `rtrim` | `(str: string)` | `string` |
| `contains` | `(str: string, needle: string)` | `bool` |

### gucc.profiles

| Function | Signature | Returns |
|-|-|-|
| `parse_base_profiles` | `(config_content: string)` | `table\|nil` |
| `parse_desktop_profiles` | `(config_content: string)` | `table\|nil` |
| `parse_net_profiles` | `(config_content: string)` | `table\|nil` |

### gucc.cpu

| Function | Signature | Returns |
|-|-|-|
| `get_vendor` | `()` | `string` — "Intel", "AMD", "Unknown" |
| `get_isa_levels` | `()` | `table` |

## Partition Table Format

Partition data is exchanged as plain Lua tables:

```lua
{
    fstype = "btrfs",                -- filesystem type
    mountpoint = "/",                -- mount path
    uuid_str = "abcd-1234-5678",     -- partition UUID
    device = "/dev/nvme0n1p2",       -- device path
    size = "100G",                   -- partition size (optional)
    mount_opts = "defaults,noatime", -- mount options (optional)
    subvolume = "@/",                -- btrfs subvolume name (optional)
    luks_mapper_name = "cryptroot",  -- LUKS mapper name (optional)
    luks_uuid = "efgh-9012",         -- LUKS UUID (optional)
    luks_passphrase = "secret",      -- LUKS passphrase (optional)
}
```


## Calamares Module Migration Guide

The following table maps Calamares Python modules and shell scripts to equivalent GUCC Lua API calls. This enables rewriting Calamares modules as standalone Lua scripts.

### Module-to-API Mapping

| Calamares Module | Lines | GUCC Lua Replacement | Coverage |
|-|-|-|-|
| `initcpiocfg/main.py` | 239 | `gucc.initcpio.setup()` | 100% |
| `fstab/main.py` | 436 | `gucc.fstab.generate()` + `gucc.crypttab.generate()` | 95% |
| `grubcfg/main.py` | 335 | `gucc.bootloader.gen_grub_config()` + `gucc.bootloader.write_grub_config()` | 100% |
| `localecfg/main.py` | 175 | `gucc.locale.set_locale()` | 100% |
| `hwclock/main.py` | 56 | `gucc.hwclock.set_utc()` | 95% |
| `services-systemd/main.py` | 91 | `gucc.services.enable/disable()` | 90% |
| `bootloader/main.py` | 1150 | `gucc.bootloader.install_*()` + `gucc.kernel_params.get()` | 85% |
| `mount/main.py` | 403 | `gucc.mount.mount_partition()` | 80% |
| `pacstrap/main.py` | 189 | `gucc.cpu.get_vendor()` + package list in Lua | 70% |
| `scripts/try-v3` | 45 | `gucc.cpu.get_isa_levels()` + `gucc.pacmanconf.push_repos_front()` | 90% |
| `scripts/detect-architecture` | 42 | Same as try-v3 | 90% |
| `shellprocess_enable_ufw.conf` | 7 | `gucc.services.enable("ufw", root)` | 100% |
| `shellprocess.conf` (post-install) | 33 | `gucc.bootloader.post_setup()` + user ops | 80% |

### Example: Replacing `initcpiocfg/main.py` (239 lines Python → 6 lines Lua)

**Before (Python, 239 lines):**
```python
# initcpiocfg/main.py - Calamares module
# Reads partition info from global storage
# Detects filesystem, encryption, lvm, swap
# Builds HOOKS/MODULES/BINARIES/FILES lists
# Writes /etc/mkinitcpio.conf
```

**After (Lua, 6 lines):**
```lua
local gucc = require "gucc"
gucc.initcpio.setup("/mnt/etc/mkinitcpio.conf", {
    filesystem_type = "btrfs",
    is_luks = true,
    has_swap = true,
    has_plymouth = true,
    use_systemd_hook = true,
})
```

### Example: Replacing `scripts/try-v3` (45 lines bash → 8 lines Lua)

**Before (Bash, 45 lines):**
```bash
check_v3="$(/lib/ld-linux-x86-64.so.2 --help | grep 'x86-64-v3 (' | awk '{print $1}')"
check_v4="$(/lib/ld-linux-x86-64.so.2 --help | grep 'x86-64-v4 (' | awk '{print $1}')"
# ... 40 more lines of sed/mv operations
```

**After (Lua, 8 lines):**
```lua
local gucc = require "gucc"
local levels = gucc.cpu.get_isa_levels()
local arch = "x86_64"
for _, level in ipairs(levels) do
    if level == "x86-64-v4" then arch = "x86-64-v4"; break end
    if level == "x86-64-v3" then arch = "x86-64-v3"; break end
end
gucc.repos.install_cachyos_repos()
```

### Example: Replacing `services-systemd/main.py` (91 lines Python → 8 lines Lua)

**Before (Python, 91 lines):**
```python
# services-systemd/main.py - Calamares module
# Reads services list from config
# Runs systemctl enable/disable for each in chroot
```

**After (Lua, 8 lines):**
```lua
local gucc = require "gucc"
local services = {"NetworkManager", "bluetooth", "cups", "avahi-daemon",
                  "systemd-timesyncd", "fstrim.timer"}
for _, svc in ipairs(services) do
    gucc.services.enable(svc, "/mnt")
end
```

### Example: Replacing `fstab/main.py` (436 lines Python → 3 lines Lua)

**Before (Python, 436 lines):**
```python
# fstab/main.py - Calamares module
# Generates fstab entries from partition info
# Handles btrfs subvolumes, SSD detection, mount options
# Generates crypttab entries for LUKS partitions
```

**After (Lua, 3 lines):**
```lua
local gucc = require "gucc"
gucc.fstab.generate(partitions, "/mnt")
gucc.crypttab.generate(partitions, "/mnt", "luks")
```

### Full Installation Pipeline

See `examples/calamares_full_install.lua` for a complete CachyOS installation pipeline that replaces 8+ Calamares modules and 5+ shell scripts in a single ~150-line Lua script.

## Build Integration

### Meson

```bash
meson setup build -Dbuild_lua_bindings=true
ninja -C build
```

The shared library `gucc.so` is installed to your library path. Set `LUA_CPATH` or copy to Lua's search path:

```bash
export LUA_CPATH="/usr/lib/lua/5.4/?.so;;"
lua -e "require 'gucc'"
```

### CMake

```bash
cmake -B build -DCOS_BUILD_LUA_BINDINGS=ON
cmake --build build
```

## Testing

```bash
lua gucc/lua/tests/test_basic.lua
```
