#include "gucc/repos.hpp"
#include "gucc/cpu.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/pacmanconf_repo.hpp"

#include <algorithm>    // for contains
#include <filesystem>   // for rename,copy_file
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace {

static constexpr auto CACHYOS_V1_REPO_STR     = R"(
[cachyos]
Include = /etc/pacman.d/cachyos-mirrorlist
)"sv;
static constexpr auto CACHYOS_V3_REPO_STR     = R"(
[cachyos-v3]
Include = /etc/pacman.d/cachyos-v3-mirrorlist
[cachyos-core-v3]
Include = /etc/pacman.d/cachyos-v3-mirrorlist
[cachyos-extra-v3]
Include = /etc/pacman.d/cachyos-v3-mirrorlist
)"sv;
static constexpr auto CACHYOS_V4_REPO_STR     = R"(
[cachyos-v4]
Include = /etc/pacman.d/cachyos-v4-mirrorlist
[cachyos-core-v4]
Include = /etc/pacman.d/cachyos-v4-mirrorlist
[cachyos-extra-v4]
Include = /etc/pacman.d/cachyos-v4-mirrorlist
)"sv;
static constexpr auto CACHYOS_ZNVER4_REPO_STR = R"(
[cachyos-znver4]
Include = /etc/pacman.d/cachyos-v4-mirrorlist
[cachyos-core-znver4]
Include = /etc/pacman.d/cachyos-v4-mirrorlist
[cachyos-extra-znver4]
Include = /etc/pacman.d/cachyos-v4-mirrorlist
)"sv;

auto add_arch_specific_repo(std::string_view isa_level, std::string_view repo_name, const std::vector<std::string>& isa_levels, std::string_view repos_data) noexcept -> bool {
    // Check if the repo ISA level is supported by the CPU
    if (!std::ranges::contains(isa_levels, isa_level)) {
        spdlog::warn("{} is not supported", isa_level);
        return true;
    }
    spdlog::info("{} is supported", isa_level);

    // Check if it's already been applied
    const auto& repo_list = gucc::detail::pacmanconf::get_repo_list("/etc/pacman.conf"sv);
    if (std::ranges::contains(repo_list, fmt::format(FMT_COMPILE("[{}]"), repo_name))) {
        spdlog::info("'{}' is already added!", repo_name);
        return true;
    }

    // Common pacmanconf working paths
    static constexpr auto pacman_conf             = "/etc/pacman.conf"sv;
    static constexpr auto pacman_conf_cachyos     = "./pacman.conf"sv;
    static constexpr auto pacman_conf_path_backup = "/etc/pacman.conf.bak"sv;

    // Create local(working) copy of pacmanconf
    std::error_code err{};
    if (!fs::copy_file(pacman_conf, pacman_conf_cachyos, err)) {
        spdlog::error("Failed to copy pacman config [{}]", err.message());
        return false;
    }

    // Add repo to local(working) copy of pacmanconf
    gucc::detail::pacmanconf::push_repos_front(pacman_conf_cachyos, repos_data);

    // Create backup of old config before replacing with new one
    spdlog::info("backup old config");
    fs::rename(pacman_conf, pacman_conf_path_backup, err);
    if (err) {
        spdlog::error("Failed to backup old config {}->{}: {}", pacman_conf, pacman_conf_path_backup, err.message());
        return false;
    }

    // Replace system pacmanconf with new one
    spdlog::info("CachyOS {} Repo changed", repo_name);
    fs::rename(pacman_conf_cachyos, pacman_conf, err);
    if (err) {
        spdlog::error("Failed to move local pacman conf into system {}->{}: {}", pacman_conf_cachyos, pacman_conf, err.message());
        return false;
    }
    return true;
}

auto check_supported_znver45() noexcept -> bool {
    return gucc::utils::exec_checked("gcc -march=native -Q --help=target 2>&1 | head -n 35 | grep -E '(znver4|znver5)' > /dev/null"sv);
}

}  // namespace

namespace gucc::repos {

auto install_cachyos_repos() noexcept -> bool {
    // fetch cachyos keyring, in some cases required on the ISO
    if (!utils::exec_checked("pacman-key --recv-keys F3B607488DB35A47 --keyserver keyserver.ubuntu.com 2>>/tmp/cachyos-install.log &>/dev/null"sv)) {
        spdlog::error("Failed to receive repo keys with pacman-key");
        return false;
    }
    if (!utils::exec_checked("pacman-key --lsign-key F3B607488DB35A47 2>>/tmp/cachyos-install.log &>/dev/null"sv)) {
        spdlog::error("Failed to locally sign repo keys with pacman-key");
        return false;
    }

    // fetch supported CPU ISA levels
    const auto& isa_levels = cpu::get_isa_levels();
    if (!add_arch_specific_repo("x86_64"sv, "cachyos"sv, isa_levels, CACHYOS_V1_REPO_STR)) {
        spdlog::error("Failed to add x86_64 cachyos repo");
        return false;
    }

    // Oracle VM doesn't support ISA levels
    if (utils::exec_checked("systemd-detect-virt | grep -q oracle"sv)) {
        spdlog::info("Oracle VM detected. skipping ISA specific repos");
        return true;
    }

    // 1. check ZNVER4/ZNVER5
    if (check_supported_znver45()) {
        if (!add_arch_specific_repo("x86_64_v4"sv, "cachyos-znver4"sv, isa_levels, CACHYOS_ZNVER4_REPO_STR)) {
            spdlog::error("Failed to add znver4 cachyos repo");
            return false;
        }
        return true;
    }
    // 2. check x86_64_v4
    if (std::ranges::contains(isa_levels, "x86_64_v4"sv)) {
        if (!add_arch_specific_repo("x86_64_v4"sv, "cachyos-v4"sv, isa_levels, CACHYOS_V4_REPO_STR)) {
            spdlog::error("Failed to add v4 cachyos repo");
            return false;
        }
        return true;
    }
    // 3. check x86_64_v3
    if (std::ranges::contains(isa_levels, "x86_64_v3"sv)) {
        if (!add_arch_specific_repo("x86_64_v3"sv, "cachyos-v3"sv, isa_levels, CACHYOS_V3_REPO_STR)) {
            spdlog::error("Failed to add v3 cachyos repo");
            return false;
        }
        return true;
    }
    return true;
}

}  // namespace gucc::repos
