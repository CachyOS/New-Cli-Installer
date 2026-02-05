#ifndef LOCALE_HPP
#define LOCALE_HPP

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::locale {

// Set system language
auto set_locale(std::string_view locale, std::string_view mountpoint) noexcept -> bool;

// Prepare system language.
// Sets without updating system locale
auto prepare_locale_set(std::string_view locale, std::string_view mountpoint) noexcept -> bool;

// List possible locales
auto get_possible_locales() noexcept -> std::vector<std::string>;

// Set X11 keyboard layout
auto set_xkbmap(std::string_view xkbmap, std::string_view mountpoint) noexcept -> bool;

// Get available X11 keymap layouts from localectl
auto get_x11_keymap_layouts() noexcept -> std::vector<std::string>;

/// @brief Parse locale.gen content and extract UTF-8 locales
/// @param content Raw content of /etc/locale.gen
/// @return vector of locale names
auto parse_locale_gen(std::string_view content) noexcept -> std::vector<std::string>;

}  // namespace gucc::locale

#endif  // LOCALE_HPP
