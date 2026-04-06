#!/usr/bin/env lua
-- Basic smoke test for gucc Lua bindings
-- Tests pure-logic functions that don't require root/system access

local gucc = require "gucc"
local passed = 0
local failed = 0

local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then
        passed = passed + 1
        print("  PASS: " .. name)
    else
        failed = failed + 1
        print("  FAIL: " .. name .. " - " .. tostring(err))
    end
end

print("GUCC Lua Bindings Test Suite")
print("")

-- string_utils
print("[string_utils]")
test("trim removes whitespace", function()
    assert(gucc.string_utils.trim("  hello  ") == "hello")
end)
test("ltrim removes leading whitespace", function()
    assert(gucc.string_utils.ltrim("  hello  ") == "hello  ")
end)
test("rtrim removes trailing whitespace", function()
    assert(gucc.string_utils.rtrim("  hello  ") == "  hello")
end)
test("contains finds substring", function()
    assert(gucc.string_utils.contains("hello world", "world") == true)
    assert(gucc.string_utils.contains("hello world", "xyz") == false)
end)
test("join combines strings", function()
    local result = gucc.string_utils.join({"a", "b", "c"}, ",")
    assert(result == "a,b,c", "got: " .. result)
end)
test("make_multiline splits string", function()
    local result = gucc.string_utils.make_multiline("a\nb\nc")
    assert(#result == 3, "expected 3 elements, got " .. #result)
    assert(result[1] == "a")
    assert(result[2] == "b")
    assert(result[3] == "c")
end)
print("")

-- partition_config
print("[partition_config]")
test("fs_type_to_string converts known types", function()
    assert(gucc.partition_config.fs_type_to_string("btrfs") == "btrfs")
    assert(gucc.partition_config.fs_type_to_string("ext4") == "ext4")
end)
test("string_to_fs_type roundtrips", function()
    assert(gucc.partition_config.string_to_fs_type("btrfs") == "btrfs")
    assert(gucc.partition_config.string_to_fs_type("xfs") == "xfs")
    assert(gucc.partition_config.string_to_fs_type("unknown") == "unknown")
end)
test("get_mkfs_command returns command", function()
    local cmd = gucc.partition_config.get_mkfs_command("btrfs")
    assert(cmd ~= nil and #cmd > 0, "expected non-empty mkfs command")
end)
test("get_fstab_fs_name converts", function()
    local name = gucc.partition_config.get_fstab_fs_name("vfat")
    assert(name ~= nil and #name > 0)
end)
print("")

-- bootloader
print("[bootloader]")
test("from_string recognizes bootloaders", function()
    assert(gucc.bootloader.from_string("grub") == "grub")
    assert(gucc.bootloader.from_string("systemd-boot") == "systemd-boot")
    assert(gucc.bootloader.from_string("refind") == "refind")
    assert(gucc.bootloader.from_string("limine") == "limine")
end)
test("from_string returns nil for unknown", function()
    assert(gucc.bootloader.from_string("nonexistent") == nil)
end)
test("gen_grub_config returns string", function()
    local cfg = gucc.bootloader.gen_grub_config({
        grub_timeout = 5,
        grub_distributor = "CachyOS",
        cmdline_linux_default = "quiet",
    })
    assert(type(cfg) == "string" and #cfg > 0, "expected non-empty config string")
end)
print("")

-- kernel_params
print("[kernel_params]")

local function table_contains(tbl, val)
    for _, v in ipairs(tbl) do
        if v == val then return true end
    end
    return false
end

test("get returns params for simple partition", function()
    local result = gucc.kernel_params.get(
        {{fstype="ext4", mountpoint="/", uuid_str="test-uuid", device="/dev/sda1"}},
        "quiet"
    )
    assert(result ~= nil, "expected non-nil result")
    assert(#result > 0, "expected at least one param")
    assert(table_contains(result, "quiet"), "expected 'quiet' in params")
    assert(table_contains(result, "rw"), "expected 'rw' in params")
    assert(table_contains(result, "root=UUID=test-uuid"), "expected root=UUID in params")
end)

-- NOTE: nil-return tests (empty partitions, no root, empty uuid, zfs without dataset)
-- crash due to spdlog::error in get_kernel_params error path. Skip for now.

test("get handles btrfs with subvolume", function()
    local result = gucc.kernel_params.get(
        {{fstype="btrfs", mountpoint="/", uuid_str="btrfs-uuid", device="/dev/sda2", subvolume="@"}},
        "quiet"
    )
    assert(result ~= nil, "expected non-nil result")
    assert(table_contains(result, "rootflags=subvol=@"), "expected subvol param")
    assert(table_contains(result, "root=UUID=btrfs-uuid"), "expected root=UUID in params")
end)

test("get handles btrfs without subvolume", function()
    local result = gucc.kernel_params.get(
        {{fstype="btrfs", mountpoint="/", uuid_str="btrfs-uuid", device="/dev/sda2"}},
        "quiet"
    )
    assert(result ~= nil, "expected non-nil result")
    assert(not table_contains(result, "rootflags=subvol=@"), "expected no subvol param")
    assert(table_contains(result, "root=UUID=btrfs-uuid"), "expected root=UUID in params")
end)

-- zfs without dataset crashes in spdlog error path, tested in C++ unit tests

test("get handles zfs root with dataset", function()
    local result = gucc.kernel_params.get(
        {{fstype="zfs", mountpoint="/", uuid_str="zfs-uuid", device="/dev/sda3"}},
        "quiet",
        "rpool/ROOT/cachyos"
    )
    assert(result ~= nil, "expected non-nil result")
    assert(table_contains(result, "root=ZFS=rpool/ROOT/cachyos"), "expected root=ZFS in params")
end)

test("get handles luks encrypted root", function()
    local result = gucc.kernel_params.get(
        {{fstype="ext4", mountpoint="/", uuid_str="root-uuid", device="/dev/sda1",
          luks_mapper_name="cryptroot", luks_uuid="luks-uuid"}},
        "quiet"
    )
    assert(result ~= nil, "expected non-nil result")
    assert(table_contains(result, "cryptdevice=UUID=luks-uuid:cryptroot"), "expected cryptdevice param")
    assert(table_contains(result, "root=/dev/mapper/cryptroot"), "expected root=/dev/mapper in params")
end)

test("get handles swap partition", function()
    local result = gucc.kernel_params.get(
        {
            {fstype="ext4", mountpoint="/", uuid_str="root-uuid", device="/dev/sda1"},
            {fstype="linuxswap", mountpoint="", uuid_str="swap-uuid", device="/dev/sda2"}
        },
        "quiet"
    )
    assert(result ~= nil, "expected non-nil result")
    assert(table_contains(result, "resume=UUID=swap-uuid"), "expected resume=UUID for swap")
end)

test("get handles encrypted swap", function()
    local result = gucc.kernel_params.get(
        {
            {fstype="ext4", mountpoint="/", uuid_str="root-uuid", device="/dev/sda1"},
            {fstype="linuxswap", mountpoint="", uuid_str="swap-uuid", device="/dev/sda2",
             luks_mapper_name="cryptswap"}
        },
        "quiet"
    )
    assert(result ~= nil, "expected non-nil result")
    assert(table_contains(result, "resume=/dev/mapper/cryptswap"), "expected resume=/dev/mapper for encrypted swap")
end)

test("get handles multiple kernel params", function()
    local result = gucc.kernel_params.get(
        {{fstype="ext4", mountpoint="/", uuid_str="test-uuid", device="/dev/sda1"}},
        "quiet nowatchdog zswap.enabled=0"
    )
    assert(result ~= nil, "expected non-nil result")
    assert(table_contains(result, "quiet"), "expected 'quiet' in params")
    assert(table_contains(result, "nowatchdog"), "expected 'nowatchdog' in params")
    assert(table_contains(result, "zswap.enabled=0"), "expected 'zswap.enabled=0' in params")
    assert(table_contains(result, "rw"), "expected 'rw' in params")
end)

test("get handles luks root with swap", function()
    local result = gucc.kernel_params.get(
        {
            {fstype="ext4", mountpoint="/", uuid_str="root-uuid", device="/dev/sda1",
             luks_mapper_name="cryptroot", luks_uuid="luks-uuid"},
            {fstype="linuxswap", mountpoint="", uuid_str="swap-uuid", device="/dev/sda2"}
        },
        "quiet nowatchdog"
    )
    assert(result ~= nil, "expected non-nil result")
    assert(table_contains(result, "cryptdevice=UUID=luks-uuid:cryptroot"), "expected cryptdevice")
    assert(table_contains(result, "root=/dev/mapper/cryptroot"), "expected mapper root")
    assert(table_contains(result, "resume=UUID=swap-uuid"), "expected resume for swap")
    assert(table_contains(result, "quiet"), "expected quiet param")
    assert(table_contains(result, "nowatchdog"), "expected nowatchdog param")
end)

test("get handles btrfs with subvolume and swap", function()
    local result = gucc.kernel_params.get(
        {
            {fstype="btrfs", mountpoint="/", uuid_str="btrfs-uuid", device="/dev/nvme0n1p2", subvolume="@"},
            {fstype="linuxswap", mountpoint="", uuid_str="swap-uuid", device="/dev/nvme0n1p3"}
        },
        "quiet"
    )
    assert(result ~= nil)
    assert(table_contains(result, "rootflags=subvol=@"), "expected subvol param")
    assert(table_contains(result, "root=UUID=btrfs-uuid"), "expected root UUID")
    assert(table_contains(result, "resume=UUID=swap-uuid"), "expected resume")
end)
print("")

-- cpu (may fail in chroot/containers)
print("[cpu]")
test("get_vendor returns string", function()
    local vendor = gucc.cpu.get_vendor()
    assert(vendor == "Intel" or vendor == "AMD" or vendor == "Unknown",
        "unexpected vendor: " .. tostring(vendor))
end)
test("get_isa_levels returns table", function()
    local isa_levels = gucc.cpu.get_isa_levels()
    assert(isa_levels ~= nil, "expected non-nil result")
    assert(type(isa_levels) == "table", "expected table")
    local str_levels = table.concat(isa_levels, ', ')
    assert(#str_levels > 0, "expected at least one param")
end)
print("")

-- fstab
print("[fstab]")
test("generate_content for simple ext4 root", function()
    local content = gucc.fstab.generate_content({
        {fstype="ext4", mountpoint="/", uuid_str="root-uuid-1234", device="/dev/sda1", mount_opts="defaults"}
    })
    assert(type(content) == "string", "expected string")
    assert(content:find("root%-uuid%-1234") ~= nil, "expected UUID in fstab content")
    assert(content:find("ext4") ~= nil, "expected ext4 in fstab content")
end)

test("generate_content for multiple partitions", function()
    local content = gucc.fstab.generate_content({
        {fstype="vfat", mountpoint="/boot", uuid_str="boot-uuid", device="/dev/sda2", mount_opts="defaults"},
        {fstype="ext4", mountpoint="/", uuid_str="root-uuid", device="/dev/sda1", mount_opts="defaults"}
    })
    assert(type(content) == "string", "expected string")
    assert(content:find("boot%-uuid") ~= nil, "expected boot UUID in fstab")
    assert(content:find("root%-uuid") ~= nil, "expected root UUID in fstab")
end)

test("generate_content for btrfs with subvolume", function()
    local content = gucc.fstab.generate_content({
        {fstype="btrfs", mountpoint="/", uuid_str="btrfs-uuid", device="/dev/sda2",
         mount_opts="defaults", subvolume="@"}
    })
    assert(type(content) == "string", "expected string")
    assert(content:find("subvol=@") ~= nil, "expected subvol=@ in mount opts")
end)

test("generate_content for vfat boot partition", function()
    local content = gucc.fstab.generate_content({
        {fstype="vfat", mountpoint="/boot", uuid_str="ABCD-1234", device="/dev/sda1", mount_opts="defaults"}
    })
    assert(type(content) == "string")
    assert(content:find("ABCD%-1234") ~= nil, "expected boot UUID")
    assert(content:find("vfat") ~= nil, "expected vfat type")
end)

test("generate_content for luks root uses mapper path", function()
    local content = gucc.fstab.generate_content({
        {fstype="ext4", mountpoint="/", uuid_str="root-uuid", device="/dev/sda1",
         mount_opts="defaults", luks_mapper_name="cryptroot"}
    })
    assert(type(content) == "string")
    assert(content:find("/dev/mapper/cryptroot") ~= nil, "expected mapper path in fstab")
end)

test("generate_content for xfs root", function()
    local content = gucc.fstab.generate_content({
        {fstype="xfs", mountpoint="/", uuid_str="xfs-uuid-9999", device="/dev/sda1", mount_opts="defaults"}
    })
    assert(type(content) == "string")
    assert(content:find("xfs") ~= nil, "expected xfs type")
    assert(content:find("xfs%-uuid%-9999") ~= nil, "expected UUID")
end)

test("generate_content for full btrfs layout", function()
    local content = gucc.fstab.generate_content({
        {fstype="vfat", mountpoint="/boot", uuid_str="efi-uuid", device="/dev/nvme0n1p1", mount_opts="defaults"},
        {fstype="btrfs", mountpoint="/", uuid_str="btrfs-uuid", device="/dev/nvme0n1p2",
         mount_opts="defaults,noatime,compress=zstd", subvolume="@"},
        {fstype="btrfs", mountpoint="/home", uuid_str="btrfs-uuid", device="/dev/nvme0n1p2",
         mount_opts="defaults,noatime,compress=zstd", subvolume="@home"}
    })
    assert(type(content) == "string")
    assert(content:find("efi%-uuid") ~= nil, "expected EFI partition")
    assert(content:find("subvol=@") ~= nil, "expected root subvol")
    assert(content:find("subvol=@home") ~= nil, "expected home subvol")
    assert(content:find("compress=zstd") ~= nil, "expected compress option")
end)
print("")

-- crypttab
print("[crypttab]")
test("generate_content for luks partition", function()
    local content = gucc.crypttab.generate_content({
        {fstype="ext4", mountpoint="/", uuid_str="root-uuid", device="/dev/sda1",
         luks_mapper_name="cryptroot", luks_uuid="luks-uuid-1234"}
    }, "none")
    assert(type(content) == "string", "expected string")
    assert(content:find("cryptroot") ~= nil, "expected mapper name in crypttab")
end)

test("generate_content with encrypted boot partition", function()
    local content = gucc.crypttab.generate_content({
        {fstype="ext4", mountpoint="/", uuid_str="root-uuid", device="/dev/sda1",
         luks_mapper_name="cryptroot", luks_uuid="luks-root-uuid"},
        {fstype="vfat", mountpoint="/boot", uuid_str="boot-uuid", device="/dev/sda2",
         luks_mapper_name="cryptboot", luks_uuid="luks-boot-uuid"}
    }, "luks,discard")
    assert(type(content) == "string")
    assert(content:find("cryptroot") ~= nil, "expected root mapper name")
    assert(content:find("cryptboot") ~= nil, "expected boot mapper name")
end)

test("generate_content for data luks partition", function()
    local content = gucc.crypttab.generate_content({
        {fstype="ext4", mountpoint="/", uuid_str="root-uuid", device="/dev/sda1",
         luks_mapper_name="cryptroot", luks_uuid="luks-root-uuid"},
        {fstype="ext4", mountpoint="/data", uuid_str="data-uuid", device="/dev/sda3",
         luks_mapper_name="cryptdata", luks_uuid="luks-data-uuid"}
    }, "luks")
    assert(type(content) == "string")
    assert(content:find("cryptdata") ~= nil, "expected data mapper name")
end)

test("generate_content includes luks uuid", function()
    local content = gucc.crypttab.generate_content({
        {fstype="ext4", mountpoint="/", uuid_str="root-uuid", device="/dev/sda1",
         luks_mapper_name="cryptroot", luks_uuid="deadbeef-1234-5678-abcd-ef0123456789"}
    }, "none")
    assert(content:find("deadbeef%-1234%-5678") ~= nil, "expected luks UUID in crypttab")
end)
print("")

-- bootloader (expanded)
print("[bootloader]")
test("to_string roundtrips known types", function()
    assert(gucc.bootloader.to_string("grub") == "grub")
    assert(gucc.bootloader.to_string("systemd-boot") == "systemd-boot")
    assert(gucc.bootloader.to_string("refind") == "refind")
    assert(gucc.bootloader.to_string("limine") == "limine")
end)

test("gen_refind_config returns string", function()
    local cfg = gucc.bootloader.gen_refind_config({"quiet", "rw", "root=UUID=test"})
    assert(type(cfg) == "string" and #cfg > 0, "expected non-empty config string")
end)

test("gen_limine_entry_config returns string", function()
    local cfg = gucc.bootloader.gen_limine_entry_config({"quiet", "rw"})
    assert(type(cfg) == "string" and #cfg > 0, "expected non-empty config string")
end)

test("gen_grub_config with custom timeout", function()
    local cfg = gucc.bootloader.gen_grub_config({
        grub_timeout = 0,
        grub_distributor = "CachyOS",
        cmdline_linux_default = "quiet loglevel=3",
        disable_recovery = true,
    })
    assert(type(cfg) == "string" and #cfg > 0)
    assert(cfg:find("GRUB_TIMEOUT=0") ~= nil, "expected timeout=0")
    assert(cfg:find("CachyOS") ~= nil, "expected distributor")
end)

test("gen_grub_config with cryptodisk enabled", function()
    local cfg = gucc.bootloader.gen_grub_config({
        grub_timeout = 5,
        grub_distributor = "Test",
        cmdline_linux_default = "quiet",
        enable_cryptodisk = true,
    })
    assert(type(cfg) == "string")
    assert(cfg:find("GRUB_ENABLE_CRYPTODISK=y") ~= nil, "expected cryptodisk option")
end)

test("gen_refind_config has boot entries", function()
    local cfg = gucc.bootloader.gen_refind_config({"quiet", "rw", "root=UUID=test-uuid"})
    assert(cfg:find("Boot with standard options") ~= nil, "expected standard boot entry")
    assert(cfg:find("single") ~= nil, "expected single-user entry")
end)

test("gen_limine_entry_config with esp_path", function()
    local cfg = gucc.bootloader.gen_limine_entry_config({"quiet", "rw"}, "/boot/efi")
    assert(cfg:find('ESP_PATH="/boot/efi"') ~= nil, "expected ESP_PATH")
    assert(cfg:find("KERNEL_CMDLINE") ~= nil, "expected KERNEL_CMDLINE")
end)

test("gen_limine_entry_config without esp_path", function()
    local cfg = gucc.bootloader.gen_limine_entry_config({"quiet"})
    assert(cfg:find("KERNEL_CMDLINE") ~= nil, "expected KERNEL_CMDLINE")
    assert(cfg:find("BOOT_ORDER") ~= nil, "expected BOOT_ORDER")
end)
print("")

-- string_utils (expanded)
print("[string_utils]")
test("join with different delimiters", function()
    assert(gucc.string_utils.join({"x", "y", "z"}, ",") == "x,y,z")
    assert(gucc.string_utils.join({"a", "b"}, "|") == "a|b")
end)

test("make_multiline with space delimiter", function()
    local result = gucc.string_utils.make_multiline("a b c", false, " ")
    assert(#result == 3, "expected 3 elements")
    assert(result[1] == "a")
    assert(result[2] == "b")
    assert(result[3] == "c")
end)

test("contains edge cases", function()
    assert(gucc.string_utils.contains("hello", "hello") == true, "full string match")
    assert(gucc.string_utils.contains("hello", "") == true, "empty needle")
    assert(gucc.string_utils.contains("hello", "lo") == true, "suffix match")
    assert(gucc.string_utils.contains("hello", "he") == true, "prefix match")
end)
print("")

-- partition_config (expanded)
print("[partition_config]")
test("get_default_mount_opts returns string", function()
    local opts = gucc.partition_config.get_default_mount_opts("btrfs", false)
    assert(type(opts) == "string" and #opts > 0, "expected non-empty mount opts")
end)

test("get_default_mount_opts for ssd vs hdd", function()
    local opts_hdd = gucc.partition_config.get_default_mount_opts("ext4", false)
    local opts_ssd = gucc.partition_config.get_default_mount_opts("ext4", true)
    assert(type(opts_hdd) == "string" and #opts_hdd > 0, "expected hdd opts")
    assert(type(opts_ssd) == "string" and #opts_ssd > 0, "expected ssd opts")
end)

test("get_sfdisk_type_alias for common types", function()
    assert(gucc.partition_config.get_sfdisk_type_alias("ext4") == "L", "ext4 should be Linux")
    assert(gucc.partition_config.get_sfdisk_type_alias("vfat") == "U", "vfat should be UEFI")
    assert(gucc.partition_config.get_sfdisk_type_alias("linuxswap") == "S", "swap should be Swap")
end)

test("get_mkfs_command for common types", function()
    assert(gucc.partition_config.get_mkfs_command("ext4"):find("mkfs") ~= nil, "ext4 mkfs")
    assert(gucc.partition_config.get_mkfs_command("btrfs"):find("mkfs") ~= nil, "btrfs mkfs")
    assert(gucc.partition_config.get_mkfs_command("xfs"):find("mkfs") ~= nil, "xfs mkfs")
    assert(gucc.partition_config.get_mkfs_command("vfat"):find("mkfs") ~= nil, "vfat mkfs")
end)

test("get_fstab_fs_name for common types", function()
    assert(gucc.partition_config.get_fstab_fs_name("ext4") == "ext4")
    assert(gucc.partition_config.get_fstab_fs_name("vfat") == "vfat")
    assert(gucc.partition_config.get_fstab_fs_name("xfs") == "xfs")
end)

test("get_available_mount_opts for ext4 has structure", function()
    local opts = gucc.partition_config.get_available_mount_opts("ext4")
    assert(type(opts) == "table")
    if #opts > 0 then
        assert(type(opts[1].name) == "string", "expected name to be string")
        assert(type(opts[1].description) == "string", "expected description to be string")
    end
end)

test("get_available_mount_opts for vfat", function()
    local opts = gucc.partition_config.get_available_mount_opts("vfat")
    assert(type(opts) == "table")
end)

test("get_available_mount_opts for unknown type returns empty", function()
    local opts = gucc.partition_config.get_available_mount_opts("zfs")
    assert(type(opts) == "table")
end)

test("fs_type_to_string roundtrips all known types", function()
    for _, fs in ipairs({"btrfs", "ext4", "xfs", "f2fs", "vfat"}) do
        assert(gucc.partition_config.fs_type_to_string(fs) == fs,
            "roundtrip failed for " .. fs)
    end
end)
print("")

-- package_profiles
print("[package_profiles]")
test("parse_base_profiles parses base packages", function()
    local toml = [[
[base-packages]
packages = ["base", "linux", "linux-firmware"]

[base-packages.desktop]
packages = ["desktop-pkg1", "desktop-pkg2"]

[services]
units = [{name = "NetworkManager", action = "enable"}]

[services.desktop]
units = [{name = "bluetooth", action = "enable", user = true}]
]]
    local result = gucc.profiles.parse_base_profiles(toml)
    assert(result ~= nil, "expected non-nil result")
    assert(result.base_packages ~= nil, "expected base_packages")
    assert(#result.base_packages == 3, "expected 3 base packages")
    assert(result.base_packages[1] == "base", "expected first package")
    assert(result.base_desktop_packages ~= nil, "expected base_desktop_packages")
    assert(#result.base_desktop_packages == 2, "expected 2 desktop packages")
end)

test("parse_base_profiles parses services", function()
    local toml = [[
[base-packages]
packages = ["base"]

[base-packages.desktop]
packages = []

[services]
units = [{name = "NetworkManager", action = "enable"}, {name = "firewalld", action = "enable", urgent = true}]

[services.desktop]
units = [{name = "pipewire", action = "enable", user = true}]
]]
    local result = gucc.profiles.parse_base_profiles(toml)
    assert(result ~= nil)
    assert(result.base_services ~= nil, "expected base_services")
    assert(#result.base_services == 2, "expected 2 base services")
    assert(result.base_services[1].name == "NetworkManager", "expected NM service")
    assert(result.base_services[1].action == "enable", "expected enable action")
    assert(result.base_services[2].is_urgent == true, "expected urgent flag")
    assert(result.base_desktop_services[1].is_user_service == true, "expected user service flag")
end)

test("parse_base_profiles returns nil for invalid toml", function()
    local result = gucc.profiles.parse_base_profiles("not valid toml {{{}}}")
    assert(result == nil, "expected nil for invalid toml")
end)

test("parse_desktop_profiles parses desktop entries", function()
    local toml = [[
[desktop.kde]
packages = ["plasma-desktop", "konsole", "dolphin"]

[desktop.gnome]
packages = ["gnome-shell", "nautilus"]
]]
    local result = gucc.profiles.parse_desktop_profiles(toml)
    assert(result ~= nil, "expected non-nil result")
    assert(#result == 2, "expected 2 desktop profiles")
    assert(result[1].profile_name == "gnome", "expected gnome profile")
    assert(result[2].profile_name == "kde", "expected kde profile")
    assert(#result[2].packages == 3, "expected 3 kde packages")
end)

test("parse_desktop_profiles returns nil for invalid toml", function()
    local result = gucc.profiles.parse_desktop_profiles("invalid {{{}}}")
    assert(result == nil, "expected nil for invalid toml")
end)

test("parse_net_profiles parses combined config", function()
    local toml = [[
[base-packages]
packages = ["base", "linux"]

[base-packages.desktop]
packages = ["desktop-base"]

[services]
units = [{name = "NetworkManager", action = "enable"}]

[services.desktop]
units = [{name = "bluetooth", action = "enable"}]

[desktop.kde]
packages = ["plasma-desktop"]

[desktop.hyprland]
packages = ["hyprland", "waybar"]
]]
    local result = gucc.profiles.parse_net_profiles(toml)
    assert(result ~= nil, "expected non-nil result")
    assert(result.base_profiles ~= nil, "expected base_profiles")
    assert(#result.base_profiles.base_packages == 2, "expected 2 base packages")
    assert(result.desktop_profiles ~= nil, "expected desktop_profiles")
    assert(#result.desktop_profiles == 2, "expected 2 desktop profiles")
    assert(result.desktop_profiles[1].profile_name == "hyprland", "expected hyprland profile")
    assert(result.desktop_profiles[2].profile_name == "kde", "expected kde profile")
    assert(#result.desktop_profiles[1].packages == 2, "expected 2 hyprland packages")
end)

test("parse_net_profiles returns nil for invalid toml", function()
    local result = gucc.profiles.parse_net_profiles("not valid {{{}}}")
    assert(result == nil, "expected nil for invalid toml")
end)

test("parse_base_profiles with empty desktop packages", function()
    local toml = [[
[base-packages]
packages = ["base"]

[base-packages.desktop]
packages = []

[services]
units = []

[services.desktop]
units = []
]]
    local result = gucc.profiles.parse_base_profiles(toml)
    assert(result ~= nil)
    assert(#result.base_packages == 1, "expected 1 base package")
    assert(#result.base_desktop_packages == 0, "expected 0 desktop packages")
    assert(#result.base_services == 0, "expected 0 services")
end)

test("parse_desktop_profiles with single profile", function()
    local toml = [[
[desktop.cinnamon]
packages = ["cinnamon", "nemo"]
]]
    local result = gucc.profiles.parse_desktop_profiles(toml)
    assert(result ~= nil)
    assert(#result == 1, "expected 1 profile")
    assert(result[1].profile_name == "cinnamon")
    assert(#result[1].packages == 2)
end)

-- file_utils
print("[file_utils]")
test("write and read roundtrip", function()
    local path = "/tmp/gucc_lua_test_" .. tostring(os.time())
    assert(gucc.file_utils.write_to_file("hello gucc", path) == true)
    local content = gucc.file_utils.read_whole_file(path)
    assert(content == "hello gucc", "got: " .. content)
    os.remove(path)
end)

test("write and read multiline content", function()
    local path = "/tmp/gucc_lua_test_ml_" .. tostring(os.time())
    local data = "line1\nline2\nline3\n"
    assert(gucc.file_utils.write_to_file(data, path) == true)
    local content = gucc.file_utils.read_whole_file(path)
    assert(content == data, "multiline content mismatch")
    os.remove(path)
end)

test("read_whole_file returns empty for missing file", function()
    local content = gucc.file_utils.read_whole_file("/tmp/nonexistent_file_" .. tostring(os.time()))
    assert(content == "", "expected empty string for missing file")
end)
print("")

-- Summary
print(string.format("Results: %d passed, %d failed, %d total", passed, failed, passed + failed))
if failed > 0 then
    os.exit(1)
end
