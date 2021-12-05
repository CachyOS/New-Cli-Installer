#ifndef UTILS_HPP
#define UTILS_HPP

#include "definitions.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace utils {
void print_banner() noexcept;
[[nodiscard]] bool is_connected() noexcept;
bool prompt_char(const char* prompt, const char* color = RESET, char* read = nullptr) noexcept;
void clear_screen() noexcept;
[[nodiscard]] auto make_multiline(std::string& str, const std::string_view&& delim = "\n") noexcept -> std::vector<std::string>;
void secure_wipe() noexcept;

void exec(const std::vector<std::string>& vec) noexcept;
auto exec(const std::string_view& command, const bool& interactive = false) noexcept -> std::string;
[[nodiscard]] bool check_root() noexcept;
void id_system() noexcept;
[[nodiscard]] bool handle_connection() noexcept;
void show_iwctl() noexcept;
}  // namespace utils

#endif  // UTILS_HPP
