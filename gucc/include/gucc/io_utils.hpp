#ifndef IO_UTILS_HPP
#define IO_UTILS_HPP

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::utils {

auto safe_getenv(const char* env_name) noexcept -> std::string_view;
void exec(const std::vector<std::string>& vec) noexcept;
auto exec(std::string_view command, bool interactive = false) noexcept -> std::string;
auto exec_checked(std::string_view command) noexcept -> bool;
void arch_chroot(std::string_view command, std::string_view mountpoint, bool interactive = false) noexcept;
auto arch_chroot_checked(std::string_view command, std::string_view mountpoint) noexcept -> bool;
auto arch_chroot_follow(std::string_view command, std::string_view mountpoint) noexcept -> bool;
auto run_pacstrap(std::string_view mountpoint, std::string_view packages, std::string_view pacman_config, bool hostcache) noexcept -> bool;

/// wait for the kernel/udev to catch up with device changes
void settle_devices() noexcept;

}  // namespace gucc::utils

#endif  // IO_UTILS_HPP
