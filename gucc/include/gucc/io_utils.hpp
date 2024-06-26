#ifndef IO_UTILS_HPP
#define IO_UTILS_HPP

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::utils {

auto safe_getenv(const char* env_name) noexcept -> std::string_view;
void exec(const std::vector<std::string>& vec) noexcept;
auto exec(std::string_view command, bool interactive = false) noexcept -> std::string;
void arch_chroot(std::string_view command, std::string_view mountpoint, bool interactive = false) noexcept;

}  // namespace gucc::utils

#endif  // IO_UTILS_HPP
