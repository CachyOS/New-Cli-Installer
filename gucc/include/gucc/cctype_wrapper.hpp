#pragma once

namespace gucc::utils {

// local-independent cctype wrapper constexpr variants.
[[nodiscard]] constexpr auto is_lower(char ch) noexcept -> bool { return ch >= 'a' && ch <= 'z'; }
[[nodiscard]] constexpr auto is_upper(char ch) noexcept -> bool { return ch >= 'A' && ch <= 'Z'; }
[[nodiscard]] constexpr auto is_digit(char ch) noexcept -> bool { return ch >= '0' && ch <= '9'; }
[[nodiscard]] constexpr auto is_alpha(char ch) noexcept -> bool { return is_lower(ch) || is_upper(ch); }
[[nodiscard]] constexpr auto is_alnum(char ch) noexcept -> bool { return is_alpha(ch) || is_digit(ch); }
[[nodiscard]] constexpr auto to_lower(char ch) noexcept -> char { return is_upper(ch) ? static_cast<char>(ch - 'A' + 'a') : ch; }
[[nodiscard]] constexpr auto to_upper(char ch) noexcept -> char { return is_lower(ch) ? static_cast<char>(ch - 'a' + 'A') : ch; }

}  // namespace gucc::utils
