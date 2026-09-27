#ifndef IO_UTILS_HPP
#define IO_UTILS_HPP

#include "gucc/error.hpp"

#include <optional>     // for optional
#include <span>         // for span
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::utils {

auto safe_getenv(const char* env_name) noexcept -> std::string_view;

auto exec(std::string_view command) noexcept -> std::string;
auto query_checked(std::string_view command) noexcept -> bool;
auto exec_checked(std::string_view command) noexcept -> bool;
auto exec_checked_with_input(std::string_view command, std::string_view input) noexcept -> bool;
auto exec_with_input(std::string_view command, std::string_view input) noexcept -> std::optional<std::string>;

void exec(const std::vector<std::string>& argv) noexcept;
auto query(const std::vector<std::string>& argv) noexcept -> std::string;
auto exec_checked(const std::vector<std::string>& argv) noexcept -> bool;
auto exec_checked(const std::vector<std::string>& argv, std::span<const std::string> env) noexcept -> bool;

void arch_chroot(std::string_view command, std::string_view mountpoint) noexcept;
auto arch_chroot_checked(std::string_view command, std::string_view mountpoint) noexcept -> bool;
auto arch_chroot_checked(const std::vector<std::string>& argv, std::string_view mountpoint) noexcept -> bool;
/// Output is empty in dry-run mode.
auto arch_chroot_output(std::string_view command, std::string_view mountpoint) noexcept -> std::optional<std::string>;
auto arch_chroot_query(std::string_view command, std::string_view mountpoint) noexcept -> std::string;
auto arch_chroot_query_checked(std::string_view command, std::string_view mountpoint) noexcept -> bool;

auto exec_or_error(std::string_view command, std::string context) noexcept -> Result<void>;
auto exec_or_error(const std::vector<std::string>& argv, std::string context) noexcept -> Result<void>;
auto exec_or_error(const std::vector<std::string>& argv, std::span<const std::string> env, std::string context) noexcept -> Result<void>;
auto exec_or_error_with_input(std::string_view command, std::string_view input, std::string context) noexcept -> Result<void>;
auto arch_chroot_or_error(std::string_view command, std::string_view mountpoint, std::string context) noexcept -> Result<void>;
auto arch_chroot_or_error(const std::vector<std::string>& argv, std::string_view mountpoint, std::string context) noexcept -> Result<void>;

auto run_pacstrap(std::string_view mountpoint, std::string_view packages, std::string_view pacman_config, bool hostcache) noexcept -> bool;

/// wait for the kernel/udev to catch up with device changes
void settle_devices() noexcept;

}  // namespace gucc::utils

#endif  // IO_UTILS_HPP
