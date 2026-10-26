#include "gucc/pacman.hpp"
#include "gucc/io_utils.hpp"

#include <array>    // for array
#include <string>   // for string
#include <utility>  // for move
#include <vector>   // for vector

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

auto pacman_argv(std::span<const std::string_view> flags, std::span<const std::string> pkgs) noexcept -> std::vector<std::string> {
    std::vector<std::string> argv{"/usr/bin/pacman"};
    argv.insert(argv.end(), flags.begin(), flags.end());
    argv.append_range(pkgs);
    return argv;
}

}  // namespace

namespace gucc::pacman {

auto is_installed(std::string_view pkg, std::optional<std::string_view> root) noexcept -> bool {
    const std::vector<std::string> argv{"/usr/bin/pacman"s, "-Qq"s, std::string{pkg}};
    return root ? utils::arch_chroot_query_checked(argv, *root) : utils::query_checked(argv);
}

auto install(std::span<const std::string> pkgs) noexcept -> Result<void> {
    if (pkgs.empty()) {
        return {};
    }
    // shouldn't be used anyways.. use pacstrap for installing
    static constexpr std::array kFlags{"-Sy"sv, "--noconfirm"sv};
    return utils::exec_or_error(pacman_argv(kFlags, pkgs), "install packages"sv);
}

auto ensure_installed(std::string_view pkg) noexcept -> Result<void> {
    if (is_installed(pkg)) {
        return {};
    }
    const std::array pkgs{std::string{pkg}};
    return install(pkgs);
}

auto remove(std::span<const std::string> pkgs, std::string_view root) noexcept -> Result<void> {
    if (pkgs.empty()) {
        return {};
    }
    static constexpr std::array kFlags{"-Rsn"sv, "--noconfirm"sv};
    return utils::arch_chroot_or_error(pacman_argv(kFlags, pkgs), root, "remove packages"sv);
}

}  // namespace gucc::pacman
