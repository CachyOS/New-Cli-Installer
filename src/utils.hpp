#ifndef UTILS_HPP
#define UTILS_HPP

#include "definitions.hpp"

#include <string_view>

namespace utils {
void print_banner() noexcept;
bool is_connected() noexcept;
bool prompt_char(const char* prompt, char& read, const char* color = RESET) noexcept;

std::string exec(const std::string_view& command) noexcept;
}  // namespace utils

#endif  // UTILS_HPP
