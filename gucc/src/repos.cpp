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

// Prepend an ISA-specific CachyOS repo into `config_path` when the CPU supports
// `isa_level` and the repo isn't already present.
auto add_arch_specific_repo(std::string_view config_path, std::string_view isa_level, std::string_view repo_name, const std::vector<std::string>& isa_levels, std::string_view repos_data) noexcept -> gucc::Result<void> {
    // Check if the repo ISA level is supported by the CPU
    if (!std::ranges::contains(isa_levels, isa_level)) {
        spdlog::warn("{} is not supported", isa_level);
        return {};
    }
    spdlog::info("{} is supported", isa_level);

    // Check if it's already been applied
    const auto& repo_list = gucc::detail::pacmanconf::get_repo_list(config_path);
    if (std::ranges::contains(repo_list, fmt::format(FMT_COMPILE("[{}]"), repo_name))) {
        spdlog::info("'{}' is already added!", repo_name);
        return {};
    }

    if (!gucc::detail::pacmanconf::push_repos_front(config_path, repos_data)) {
        return gucc::make_error(gucc::ErrorCode::FileIo, fmt::format("Failed to add CachyOS {} repo to {}", repo_name, config_path));
    }
    spdlog::info("CachyOS {} Repo added to {}", repo_name, config_path);
    return {};
}

auto apply_cachyos_repos(std::string_view config_path) noexcept -> gucc::Result<void> {
    // fetch supported CPU ISA levels
    const auto& isa_levels = gucc::cpu::get_isa_levels();
    if (auto res = add_arch_specific_repo(config_path, "x86_64"sv, "cachyos"sv, isa_levels, CACHYOS_V1_REPO_STR); !res) {
        return res;
    }

    // Oracle VM doesn't support ISA levels
    if (gucc::utils::exec_checked("systemd-detect-virt | grep -q oracle"sv)) {
        spdlog::info("Oracle VM detected. skipping ISA specific repos");
        return {};
    }

    // 1. check ZNVER4/ZNVER5
    if (std::ranges::contains(isa_levels, "znver4"sv)) {
        return add_arch_specific_repo(config_path, "znver4"sv, "cachyos-znver4"sv, isa_levels, CACHYOS_ZNVER4_REPO_STR);
    }
    // 2. check x86_64_v4
    if (std::ranges::contains(isa_levels, "x86_64_v4"sv)) {
        return add_arch_specific_repo(config_path, "x86_64_v4"sv, "cachyos-v4"sv, isa_levels, CACHYOS_V4_REPO_STR);
    }
    // 3. check x86_64_v3
    if (std::ranges::contains(isa_levels, "x86_64_v3"sv)) {
        return add_arch_specific_repo(config_path, "x86_64_v3"sv, "cachyos-v3"sv, isa_levels, CACHYOS_V3_REPO_STR);
    }
    return {};
}

}  // namespace

namespace gucc::repos {

auto install_cachyos_keyring() noexcept -> Result<void> {
    // fetch cachyos keyring, in some cases required on the ISO
    if (!utils::exec_checked("pacman-key --recv-keys F3B607488DB35A47 --keyserver keyserver.ubuntu.com"sv)) {
        return make_error(ErrorCode::SubprocessFailed, "pacman-key --recv-keys failed");
    }
    if (!utils::exec_checked("pacman-key --lsign-key F3B607488DB35A47"sv)) {
        return make_error(ErrorCode::SubprocessFailed, "failed to locally sign pacman-key");
    }
    return {};
}

auto create_target_pacman_config(std::string_view base_config, std::string_view output_config) noexcept -> Result<void> {
    std::error_code err{};
    fs::copy_file(base_config, output_config, fs::copy_options::overwrite_existing, err);
    if (err) {
        return make_error(ErrorCode::FileIo, fmt::format("Failed to copy {}->{}: {}", base_config, output_config, err.message()));
    }

    return apply_cachyos_repos(output_config);
}

}  // namespace gucc::repos
