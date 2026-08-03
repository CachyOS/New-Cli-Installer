# Configuring CachyOS TUI Installer

## Quick Start

1. Write a config file (see the minimal template below, or copy one from `examples/`).
2. Run the installer as root, pointing it at your config.

**Minimal headless config.** You only need to set the machine-specific fields
and the credentials; everything else has a default (see
[Running the installer](#running-the-installer) and
[Headless defaults](#headless-defaults)):
```json
{
    "install_type": "simple",
    "headless_mode": true,
    "device": "/dev/nvme0n1",
    "fs_name": "btrfs",
    "subvolumes": "default",
    "partitions": [
        {"name": "/dev/nvme0n1p1", "mountpoint": "/boot", "size": "4G", "fs_name": "vfat", "type": "boot"},
        {"name": "/dev/nvme0n1p2", "mountpoint": "/", "size": "100%", "type": "root"}
    ],
    "user_name": "user",
    "user_pass": "password",
    "root_pass": "rootpassword",
    "desktop": "kde"
}
```

## Running the installer

The installer binary is `cachyos-installer`. It must run **as root** with an
**active network connection**.

```bash
sudo cachyos-installer --config /path/to/settings.json
```

- **`--config <path>`** read the config from `<path>`. Default: `./settings.json`
  (the current directory). A config with `"headless_mode": true` installs
  unattended; otherwise the interactive TUI starts.
- **`--dry-run`** log commands instead of running them (debug builds only).
- **`--version`** print the version and exit.
- **`--help`** show usage and exit.

With no `--config` and no `settings.json` in the current directory, the
interactive TUI starts (choose Simple or Advanced with `menus`/`install_type`).

Unknown or misspelled keys are **rejected** with an error naming the offending
key, and enumerated values (`bootloader`, `fs_name`, `hw_clock`) are validated.

## Configuration Reference

| Option | Type | Default | Required (Headless) | Description |
|--------|------|---------|---------------------|-------------|
| `menus` | int | `2` | Yes* | Install type: 1=Simple, 2=Advanced (alias: `install_type`) |
| `install_type` | string | - | Yes* | `"simple"`/`"advanced"` friendlier alias for `menus` |
| `headless_mode` | bool | `false` | - | Unattended installation mode |
| `device` | string | - | Yes | Target device (e.g., `/dev/nvme0n1`) |
| `fs_name` | string | - | Yes | Root filesystem: ext4/btrfs/xfs/f2fs/zfs |
| `partitions` | array | - | Yes'1 | Partition layout (see below) |
| `subvolumes` | string/array | `"default"` | - | Btrfs subvolume layout (see below) |
| `mount_opts` | string | auto | - | Custom mount options |
| `allow_auto_partition` | bool | `false` | - | **Erase `device` and auto-partition** when `partitions` is empty |
| `encrypt_swap` | bool | `false` | - | Encrypt the swap partition |
| `hostcache` | bool | `true` | - | Reuse the live env's package cache |
| `hostname` | string | `cachyos` | - | Machine hostname |
| `locale` | string | `en_US.UTF-8` | - | System locale |
| `xkbmap` | string | `us` | - | Keyboard layout (alias: `keymap`) |
| `timezone` | string | `UTC` | - | Timezone (e.g., `America/New_York`) |
| `user_name` | string | - | Yes | Username to create (alias: `username`) |
| `user_pass` | string | - | Yes | User password (alias: `user_password`) |
| `user_shell` | string | `/bin/bash` | - | User shell path |
| `root_pass` | string | - | Yes | Root password (alias: `root_password`) |
| `kernel` | string | `linux-cachyos` | - | Kernel(s) to install (space-separated) |
| `desktop` | string | - | Yes'2 | Desktop environment |
| `bootloader` | string | `grub` | - | Bootloader: grub/systemd-boot/refind/limine |
| `autologin` | bool | `false` | - | Enable display-manager autologin |
| `user_groups` | array | defaults | - | Supplementary groups for the user |
| `hw_clock` | string | - | - | Hardware clock: `"utc"` or `"localtime"` |
| `chwd` | bool | `true` | - | Run `chwd -a` hardware-driver profiles |
| `carry_network` | bool | `true` | - | Carry live NetworkManager connections into target |
| `os_prober` | bool | `true` | - | Let GRUB detect other installed OSes |
| `netinstall_groups` | array | - | - | Optional package groups to install (see `net-profiles.toml`) |
| `post_install` | string | - | - | Path to a post-install script (runs headless too) |
| `server_mode` | bool | `false` | - | Deprecated; legacy switch mapped to `server_profile` |
| `server_profile` | string | - | - | Server Edition role: `minimal`/`web`/`container-host`/`db`/`cockpit` |
| `ssh_authorized_keys` | array | - | Yes (server) | Administrator SSH public keys |
| `server_extra_packages` | array | - | - | Extra packages added on top of the profile |
| `server_extra_tcp_ports` | array | - | - | Extra firewall TCP ports opened |
| `server_extra_udp_ports` | array | - | - | Extra firewall UDP ports opened |
| `net_profiles_path` | string | - | - | User net-profiles overlay (URL or `file://` path) |

\* Exactly one of `menus` / `install_type` is required.
'1 Not required when `allow_auto_partition` is `true`.
'2 Not required (and must be empty) when `server_profile` is set.

### Headless defaults

In `headless_mode`, any field you leave out that has a listed default is filled
in with that default. A headless config therefore has to provide only `device`,
`fs_name`, `partitions` (unless `allow_auto_partition` is set), `user_name`,
`user_pass`, and `root_pass`. A desktop install also needs `desktop`; a server
install needs `server_profile` and `ssh_authorized_keys` instead.

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

**Deprecated.** Legacy switch that enabled a desktop-less install. It is now
reconciled with `server_profile`:

- `server_mode: true` with no `server_profile` → maps to the `minimal` profile.
- `server_mode: true` with a `server_profile` → uses that profile.
- `server_mode: false` combined with a `server_profile` → **error**.

Prefer `server_profile` in new configs.

```json
"server_mode": true
```

---

## Server Edition

Set `server_profile` to install headless server. Every profile builds on
a base profile that sets up SSH, ufw, and server networking, so you must
supply at least one `ssh_authorized_keys` entry.

### `server_profile`

| Value | Role |
|-|-|
| `minimal` | base only |
| `web` | Nginx |
| `container-host` | Docker compose |
| `cockpit` | Cockpit |

Profile packages, services and firewall ports come from
`server-profiles.toml` fetched fresh from upstream when reachable, otherwise
the vendored copy.

### User customization

Four keys add to the selected profile. `server_extra_packages` installs more
packages, `server_extra_tcp_ports` and `server_extra_udp_ports` open more
firewall ports, and `ssh_authorized_keys` sets the ssh keys.

### Firewall

Every server install enables **ufw**. SSH is rate-limited, and every port the
selected profile needs gets an `allow` rule. ufw already handles ICMP
and the DHCP client, so the firewall ends up default-deny inbound, leaving only
SSH and the profile's own services reachable.

### Headless example (Cockpit server)

```json
{
  "menus": 1,
  "headless_mode": true,
  "device": "/dev/nvme0n1",
  "fs_name": "ext4",
  "partitions": [
    { "name": "/dev/nvme0n1p2", "mountpoint": "/", "size": "450G", "type": "root" },
    { "name": "/dev/nvme0n1p1", "mountpoint": "/boot", "size": "512M", "fs_name": "vfat", "type": "boot" }
  ],
  "hostname": "cachyos-server",
  "locale": "en_US",
  "xkbmap": "us",
  "timezone": "Europe/Berlin",
  "user_name": "admin",
  "user_pass": "1234",
  "user_shell": "/bin/bash",
  "root_pass": "1234",
  "kernel": "linux-cachyos-server",
  "bootloader": "systemd-boot",
  "server_profile": "cockpit",
  "ssh_authorized_keys": [
    "ssh-ed25519 AAAA... admin@example.com"
  ]
}
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

**Valid values:** `btrfs`, `ext4`, `xfs`, `f2fs`

```json
"fs_name": "btrfs"
```

### `mount_opts`

Custom mount options. If not specified, defaults are used:

```json
"mount_opts": "compress=zstd,noatime"
```

### `subvolumes`

Btrfs subvolume layout. Only applies when root filesystem is `btrfs`.

**Default:** `"default"` (standard CachyOS layout)

**Accepted values:**

| Value | Behavior |
|-------|----------|
| `"default"` or omitted | Standard layout: `/@`, `/@home`, `/@root`, `/@srv`, `/@cache`, `/@tmp`, `/@log` |
| Array of objects | Custom subvolume definitions |

**Default layout example:**
```json
"subvolumes": "default"
```

**Custom layout example:**
```json
"subvolumes": [
    {"subvolume": "/@", "mountpoint": "/"},
    {"subvolume": "/@home", "mountpoint": "/home"},
    {"subvolume": "/@snapshots", "mountpoint": "/.snapshots"}
]
```

Each subvolume object requires:

| Field | Type | Description |
|-------|------|-------------|
| `subvolume` | string | Subvolume name (e.g., `/@home`) |
| `mountpoint` | string | Mount point in installed system (e.g., `/home`) |

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

**Valid values:** `kde`, `gnome`, `xfce`, `sway`, `wayfire`, `i3wm`, `openbox`, `bspwm`, `lxqt`

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
