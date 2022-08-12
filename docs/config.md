# Configuring CachyOS TUI installer

This is intended as a guide to the "Simple Install" configuration. The behavior of a automated install can be modified
through the many configuration settings documented here — each config option is explained,
including what the default is, how to change the default.
Also included is an example configuration for each setting. If you don't want to spend a lot of time
thinking about options, the config as generated sets sensible defaults for all values. Do note however that the
config is used only for the "Simple Install".

The config file(`settings.json`) must be placed in the current directory.
for example: if going to launch installer in `/root`, then the config must be placed at `/root/settings.json`


### JSON
The configuration file is a [JSON](https://www.json.org/json-en.html) file, which means that certain syntax rules
apply if you want your config file to be read properly. A few helpful things to know:
* JSON doesn’t allow comments! Everything in JSON is a key ⇒ value pair,
  meaning that every line should be a piece of data,
  rather than a function or a variable like in typical programming languages.

## Install types ##

Define your install type.

---
### `menus`

This sets the installation type.

The `1` number corresponeds to "Simple Install" (automated install).
The `2` number corresponeds to "Advanced Install" (manual install).
Other number will be treated as `-1`, meaning selection between "Simple Install",
and "Advanced Install" is manual by the user.

Defaults to `2`.

Example configuration #1:
```json
"menus": 1
```
Example configuration #2:
```json
"menus": -1
```
---
## Install Modes ##

Define your install mode.

---
### `headless_mode`

This set mode to `HEADLESS`.


The `HEADLESS` mode forces some options to be required.
The `headless_mode` will be reset to default value,
if "Advanced Install" is beeing used.

Defaults to `false`.

Example configuration:
```json
"headless_mode": true
```
---
## Other options ##

Define automated install options.

---
### `device`

This sets the target device.
Required in `HEADLESS` mode!

The `device` will be used as a install drive, which will be wiped,
and used to install CachyOS on.

There is no default for this option.

Example configuration:
```json
"device": "/dev/nvme0n1"
```
---
### `fs_name`

This sets the target device filesystem.
Required in `HEADLESS` mode!

The `fs_name` will be used on the `device`,
also `mount_opts` option can be used to mount the `fs_name`.

There is no default for this option.

Example configuration:
```json
"fs_name": "btrfs"
```
---
### `mount_opts`

This sets the mount options to mount `device` with.

Default to:
* btrfs: "compress=lzo,noatime,space_cache,ssd,commit=120"
* ext4: "noatime"
* xfs: no defaults for that filesystem
* zfs: this option is invalid for ZFS

Example configuration:
```json
"mount_opts": "compress=lzo,noatime,space_cache,ssd,commit=120"
```
---
### `hostname`

This sets the machine hostname `/etc/hostname`.
Required in `HEADLESS` mode!

Defaults to "cachyos".

Example configuration:
```json
"hostname": "cachyos"
```
---
### `locale`

This sets the machine locale.
Required in `HEADLESS` mode!

Locale must be valid for the glibc,
any locale from `/etc/locale.gen` is valid.

Defaults "en_US".

Example configuration:
```json
"locale": "en_US"
```
---
### `xkbmap`

This sets the desktop environment keymap.
Required in `HEADLESS` mode!

Can be used any keymap valid for the xkbmap doc.

Defaults "us".

Example configuration:
```json
"xkbmap": "us"
```
---
### `timezone`

This sets the timezone of the system.
Required in `HEADLESS` mode!

Format "ZONE/CITY".

There is no default for this option.

Example configuration:
```json
"timezone": "America/New_York"
```
---
### `user_name`

This sets the user name.
Required in `HEADLESS` mode!

Must be provided with `user_name`, `user_pass`, `user_shell`,
overwise has no effect.

There is no default for this option.

Example configuration:
```json
"user_name": "admin"
```
---
### `user_pass`

This sets the user password.
Required in `HEADLESS` mode!

Must be provided with `user_name`, `user_pass`, `user_shell`,
overwise has no effect.

There is no default for this option.

Example configuration:
```json
"user_pass": "verysecurepassword"
```
---
### 'user_shell'

This sets the user shell.
Required in `HEADLESS` mode!

Must be provided with `user_name`, `user_pass`, `user_shell`,
overwise has no effect.

There is no default for this option.

Example configuration:
```json
"user_shell": ""
```
---
### `root_pass`

This sets the root user password.
Required in `HEADLESS` mode!

There is no default for this option.

Example configuration:
```json
"root_pass": "verysecurepassword"
```
---
### `kernel`

This kernel/kernels will be installed on the system.
Required in `HEADLESS` mode!

Any kernel is valid, also can be specified multiple kernels.
NOTE: Multiple Kernels must be splitted by SPACE.

There is no default for this option.

Example configuration:
```json
"kernel": "linux-cachyos-bore linux-zen"
```
---
### `desktop`

This will install desktop environment.
Required in `HEADLESS` mode!

Valid `desktop`:
* kde
* cutefish
* xfce
* sway
* wayfire
* i3wm
* gnome
* openbox
* bspwm
* Kofuku edition
* lxqt

There is no default for this option.

Example configuration:
```json
"desktop": "kde"
```
---
### `bootloader`

This will specified bootloader on the system.
Required in `HEADLESS` mode!

Valid `bootloader` for UEFI system:
* systemd-boot
* grub
* refind

Valid `bootloader` for BIOS system:
* grub
* grub + os-prober

There is no default for this option.

Example configuration:
```json
"bootloader": "systemd-boot"
```
---
### `drivers_type`

This will detect drivers automatically and install them.

Valid types:
* nonfree
* free

Defaults to `free`.

Example configuration:
```json
"drivers_type": "free"
```
---
### `post_install`

This will be launched after install is done.

The `post_install` must be path to the post-install script,
script must be executable. The path must be absolute.

TODO: extend description.

There is no default for this option.

Example configuration:
```json
"post_install": "/root/script.py"
```
---
