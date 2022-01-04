# cachyos-new-installer
CLI net-installer for CachyOS, inspired by manjaro-architect

This installer provides online installation for CachyOS.

### Libraries used in this project

* [Functional Terminal (X) User interface](https://github.com/ArthurSonzogni/FTXUI) used for TUI.
* [A modern formatting library](https://github.com/fmtlib/fmt) used for formatting strings, output and logging.
* [Fast C++ logging library](https://github.com/gabime/spdlog) used for logging process of the installer.
* [Parsing gigabytes of JSON per second](https://github.com/simdjson/simdjson) used for config deserialization.
* [Curl for People](https://github.com/libcpr/cpr) used for connection check and maybe in future fetching netinstall config from github.
* [Ranges](https://github.com/ericniebler/range-v3) used for ranges support with clang.


**Simple menu overview:**

TODO: should be simple as calamares

---

**Advanced menu overview:**

```
Main Menu
|
├── Prepare Installation
|   ├── Set Virtual Console (TODO)
|   ├── List Devices
|   ├── Partition Disk
|   ├── RAID (WIP)
|   ├── LUKS Encryption (WIP)
|   ├── Logical Volume Management (WIP)
|   ├── Mount Partitions
|   ├── Configure Installer Mirrorlist (TODO)
|   |   ├── Edit Pacman Configuration
|   |   ├── Edit Pacman Mirror Configuration
|   |   └── Rank Mirrors by Speed
|   |
│   └── Refresh Pacman Keys (TODO)
|
├── Install System
│   ├── Install Base Packages
│   ├── Install Desktop
│   ├── Install Bootloader
│   ├── Configure Base
|   │   ├── Generate FSTAB (seems broken)
|   │   ├── Set Hostname
|   │   ├── Set System Locale (WIP)
|   │   ├── Set Timezone and Clock
|   │   ├── Set Root Password
|   │   └── Add New User(s) (seems broken)
|   │
│   ├── Install Custom Packages
│   ├── System Tweaks (TODO)
|   │   ├── Enable Automatic Login
|   │   ├── Enable Hibernation
|   │   ├── Performance
|   |   │   ├── I/O schedulers
|   |   │   ├── Swap configuration
|   |   │   └── Preload
|   |   │
|   │   ├── Security and systemd Tweaks
|   |   │   ├── Amend journald Logging
|   |   │   ├── Disable Coredump Logging
|   |   │   └── Restrict Access to Kernel Logs
|   |   │
|   │   └── Restrict Access to Kernel Logs
|   │
│   ├── Review Configuration Files (TODO)
│   └── Chroot into Installation (TODO)
|
└── System Rescue
    ├── Install Hardware Drivers (TODO)
    │   ├── Install Display Drivers
    │   └── Install Network Drivers
    |
    ├── Install Bootloader
    ├── Configure Base
    |   └── ... (see 'Install System')
    │
    ├── Install Custom Packages
    ├── Remove Packages
    ├── Review Configuration Files (TODO)
    ├── Chroot into Installation (TODO)
    ├── Data Recovery (TODO)
    │   └── Btrfs snapshots..
    │
    └── View System Logs (TODO)
        ├── Dmesg
        ├── Pacman log
        ├── Xorg log
        └── Journalctl
```