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

}  // namespace gucc::locale

#endif  // LOCALE_HPP
