#ifndef TUI_HPP
#define TUI_HPP

namespace tui {
bool exit_done() noexcept;
void set_hostname() noexcept;
void set_locale() noexcept;
void set_xkbmap() noexcept;
bool set_timezone() noexcept;
void create_new_user() noexcept;
void set_root_password() noexcept;
void mount_opts(bool force = false) noexcept;
void lvm_detect() noexcept;
bool mount_current_partition(bool force = false) noexcept;
void auto_partition(bool interactive = true) noexcept;
void create_partitions() noexcept;
bool select_device() noexcept;
void install_base() noexcept;
void install_desktop() noexcept;
void install_bootloader() noexcept;

void init() noexcept;
}  // namespace tui

#endif  // TUI_HPP
