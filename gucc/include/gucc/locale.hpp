#ifndef LOCALE_HPP
#define LOCALE_HPP

#include <string_view>  // for string_view

namespace gucc::locale {

// Set system language
auto set_locale(std::string_view locale, std::string_view mountpoint) noexcept -> bool;

}  // namespace gucc::locale

#endif  // LOCALE_HPP
