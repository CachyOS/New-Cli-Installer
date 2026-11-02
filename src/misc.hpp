#ifndef MISC_HPP
#define MISC_HPP

#include <string_view>  // for string_view

namespace tui {
bool confirm_mount([[maybe_unused]] std::string_view part_user, bool quite = false);
void set_cache() noexcept;
void edit_configs() noexcept;
void edit_pacman_conf() noexcept;
void logs_menu() noexcept;
}  // namespace tui

#endif  // MISC_HPP
