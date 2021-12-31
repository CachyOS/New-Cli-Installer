#ifndef UTILS_HPP
#define UTILS_HPP

#include "definitions.hpp"
#include "subprocess.h"

#include <charconv>     // for from_chars
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace utils {
void print_banner() noexcept;
[[nodiscard]] bool is_connected() noexcept;
bool prompt_char(const char* prompt, const char* color = RESET, char* read = nullptr) noexcept;
void clear_screen() noexcept;
[[nodiscard]] auto make_multiline(const std::string_view& str, bool reverse = false, const std::string_view&& delim = "\n") noexcept -> std::vector<std::string>;
[[nodiscard]] auto make_multiline(std::vector<std::string>& multiline, bool reverse = false, const std::string_view&& delim = "\n") noexcept -> std::string;
void secure_wipe() noexcept;
[[nodiscard]] bool check_mount() noexcept;
[[nodiscard]] bool check_base() noexcept;
[[nodiscard]] std::string list_mounted() noexcept;
[[nodiscard]] std::string list_containing_crypt() noexcept;
[[nodiscard]] std::string list_non_crypt() noexcept;
void umount_partitions() noexcept;
void find_partitions() noexcept;

void arch_chroot(const std::string_view& command, bool follow = true) noexcept;
void exec_follow(const std::vector<std::string>& vec, std::string& process_log, bool& running, subprocess_s& child, bool async = true) noexcept;
void exec(const std::vector<std::string>& vec) noexcept;
auto exec(const std::string_view& command, const bool& interactive = false) noexcept(false) -> std::string;
[[nodiscard]] bool check_root() noexcept;
void id_system() noexcept;
[[nodiscard]] bool handle_connection() noexcept;
void show_iwctl() noexcept;

void parse_config() noexcept;

template <typename T = std::int32_t,
    typename         = std::enable_if_t<std::numeric_limits<T>::is_integer>>
inline T to_int(const std::string_view& str) {
    T result = 0;
    std::from_chars(str.data(), str.data() + str.size(), result);
    return result;
}
}  // namespace utils

#endif  // UTILS_HPP
