#ifndef UTILS_HPP
#define UTILS_HPP

#include "definitions.hpp"

#include <string_view>

namespace utils {
void print_banner() noexcept;
[[nodiscard]] bool is_connected() noexcept;
bool prompt_char(const char* prompt, const char* color = RESET, char* read = nullptr) noexcept;
void clear_screen() noexcept;

auto exec(const std::string_view& command) noexcept -> std::string;
[[nodiscard]] bool check_root() noexcept;
void id_system() noexcept;
[[nodiscard]] bool handle_connection() noexcept;
void show_iwctl() noexcept;
}  // namespace utils

#endif  // UTILS_HPP
