# Configuring CachyOS TUI Installer

## Quick Start

1. Create `settings.json` in the directory where you'll run the installer (e.g., `/root/settings.json`)
2. Copy the minimal template below and modify as needed
3. Run the installer

**Minimal headless config:**
```json
{
    "menus": 1,
    "headless_mode": true,
    "device": "/dev/nvme0n1",
    "fs_name": "btrfs",
    "partitions": [
        {"name": "/dev/nvme0n1p1", "mountpoint": "/boot", "size": "4G", "fs_name": "vfat", "type": "boot"},
        {"name": "/dev/nvme0n1p2", "mountpoint": "/", "size": "100%", "type": "root"}
    ],
    "hostname": "cachyos",
    "locale": "en_US",
    "xkbmap": "us",
    "timezone": "America/New_York",
    "user_name": "user",
    "user_pass": "password",
    "user_shell": "/bin/bash",
    "root_pass": "rootpassword",
    "kernel": "linux-cachyos",
    "desktop": "kde",
    "bootloader": "systemd-boot"
}
```

## Configuration Reference

| Option | Type | Default | Required (Headless) | Description |
|--------|------|---------|---------------------|-------------|
| `menus` | int | `2` | Yes | Install type: 1=Simple, 2=Advanced |
| `headless_mode` | bool | `false` | - | Unattended installation mode |
| `server_mode` | bool | `false` | - | Enable server profile (no DE) |
| `device` | string | - | Yes | Target device (e.g., `/dev/nvme0n1`) |
| `fs_name` | string | - | Yes | Root filesystem type |
| `partitions` | array | - | Yes | Partition layout (see below) |
| `mount_opts` | string | auto | - | Custom mount options |
| `hostname` | string | - | Yes | Machine hostname |
| `locale` | string | - | Yes | System locale (e.g., `en_US`) |
| `xkbmap` | string | - | Yes | Keyboard layout (e.g., `us`) |
| `timezone` | string | - | Yes | Timezone (e.g., `America/New_York`) |
| `user_name` | string | - | Yes | Username to create |
| `user_pass` | string | - | Yes | User password |
| `user_shell` | string | - | Yes | User shell path |
| `root_pass` | string | - | Yes | Root password |
| `kernel` | string | - | Yes | Kernel(s) to install (space-separated) |
| `desktop` | string | - | Yes | Desktop environment |
| `bootloader` | string | - | Yes | Bootloader to use |
| `post_install` | string | - | - | Path to post-install script |

---

## Install Type

### `menus`

Sets the installation type.

| Value | Meaning |
|-------|---------|
| `1` | Simple Install (automated) |
| `2` | Advanced Install (manual) |
| other | User chooses at runtime |

**Default:** `2`

```json
"menus": 1
```

---

## Install Modes

### `headless_mode`

Enable unattended installation. When `true`, all required fields must be present.

**Default:** `false`

```json
"headless_mode": true
```

### `server_mode`

Enable server profile (skips desktop environment).

**Default:** `false`

```json
"server_mode": true
```

---

## Device Configuration

### `device`

Target device for installation. **Warning:** This device will be wiped!

**Required in HEADLESS mode.**

```json
"device": "/dev/nvme0n1"
```

### `partitions`

Partition layout array. Each partition requires:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Device path (e.g., `/dev/sda1`) |
| `mountpoint` | string | Yes | Mount point (e.g., `/`, `/boot`) |
| `size` | string | Yes | Size (e.g., `512M`, `100G`, `100%`) |
| `type` | string | Yes | `root`, `boot`, or `additional` |
| `fs_name` | string | Required for non-root | Filesystem type |

> **Note:** Root partitions inherit `fs_name` from the global setting if not specified.

```json
"partitions": [
    {"name": "/dev/nvme0n1p1", "mountpoint": "/boot", "size": "512M", "fs_name": "vfat", "type": "boot"},
    {"name": "/dev/nvme0n1p2", "mountpoint": "/", "size": "450G", "type": "root"}
]
```

### `fs_name`

Root filesystem type.

**Required in HEADLESS mode.**

**Valid values:** `btrfs`, `ext4`, `xfs`, `f2fs`, `zfs`

```json
"fs_name": "btrfs"
```

### `mount_opts`

Custom mount options. If not specified, sensible defaults are used:

| Filesystem | Default Options |
|------------|-----------------|
| btrfs | `compress=lzo,noatime,space_cache,ssd,commit=120` |
| ext4 | `noatime` |
| xfs | none |
| zfs | N/A (managed by ZFS) |

```json
"mount_opts": "compress=zstd,noatime"
```

---

## System Settings

### `hostname`

Machine hostname (written to `/etc/hostname`).

**Required in HEADLESS mode.** Default in interactive: `"cachyos"`

```json
"hostname": "mycomputer"
```

### `locale`

System locale. Must be valid from `/etc/locale.gen`.

**Required in HEADLESS mode.** Default in interactive: `"en_US"`

```json
"locale": "de_DE"
```

### `xkbmap`

Keyboard layout for X11/Wayland.

**Required in HEADLESS mode.** Default in interactive: `"us"`

```json
"xkbmap": "de"
```

### `timezone`

Timezone in `Zone/City` format.

**Required in HEADLESS mode.**

```json
"timezone": "Europe/Berlin"
```

---

## User Settings

### `user_name`, `user_pass`, `user_shell`

All three must be provided together.

**Required in HEADLESS mode.**

**Valid shells:** `/bin/bash`, `/usr/bin/zsh`, `/usr/bin/fish`

```json
"user_name": "admin",
"user_pass": "securepassword",
"user_shell": "/usr/bin/zsh"
```

### `root_pass`

Root password.

**Required in HEADLESS mode.**

```json
"root_pass": "verysecurepassword"
```

---

## Packages

### `kernel`

Kernel(s) to install. Multiple kernels can be specified separated by spaces.

**Required in HEADLESS mode.**

```json
"kernel": "linux-cachyos-bore linux-zen"
```

### `desktop`

Desktop environment to install.

**Required in HEADLESS mode.**

**Valid values:** `kde`, `gnome`, `xfce`, `sway`, `wayfire`, `i3wm`, `openbox`, `bspwm`, `lxqt`, `cutefish`, `Kofuku edition`

```json
"desktop": "kde"
```

### `bootloader`

Bootloader to install.

**Required in HEADLESS mode.**

| System | Valid Values |
|--------|--------------|
| UEFI | `systemd-boot`, `grub`, `refind`, `limine` |
| BIOS | `grub` |

```json
"bootloader": "systemd-boot"
```

---

## Post-Install

### `post_install`

Path to an executable script to run after installation completes.

```json
"post_install": "/root/my-setup-script.sh"
```
