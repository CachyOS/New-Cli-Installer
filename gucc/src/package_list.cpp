#include "gucc/package_list.hpp"
#include "gucc/cpu.hpp"
#include "gucc/fetch_file.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/package_profiles.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>  // for find, search
#include <ranges>     // for ranges::*
#include <string>     // for string
#include <vector>     // for erase_if

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

constinit std::optional<std::string> cached_content;  // NOLINT
constinit std::string cached_url;                     // NOLINT
constinit std::string cached_fallback_url;            // NOLINT
constinit std::string cached_user_path;               // NOLINT

// Builds the merged net-profiles document from two layers:
//  - base: regular installer net-profiles.toml
//  - user: an optional user-supplied one
auto fetch_net_profiles_cached(const gucc::package::NetProfileInfo& info) noexcept -> std::optional<std::string> {
    if (cached_content
        && cached_url == info.net_profs_url
        && cached_fallback_url == info.net_profs_fallback_url
        && cached_user_path == info.net_profs_user_path) {
        spdlog::debug("net profiles: using cached content");
        return cached_content;
    }

    auto base_content = gucc::fetch::fetch_file_from_url(info.net_profs_url, info.net_profs_fallback_url);
    if (!base_content) {
        spdlog::error("net profiles: failed to load base layer (url '{}', fallback '{}')", info.net_profs_url, info.net_profs_fallback_url);
        return std::nullopt;
    }

    // user profile
    std::optional<std::string> user_content;
    if (!info.net_profs_user_path.empty()) {
        user_content = gucc::fetch::fetch_file(info.net_profs_user_path);
        if (!user_content) {
            spdlog::warn("net profiles: could not load user layer from '{}', ignoring it", info.net_profs_user_path);
        }
    }

    std::vector<std::string_view> layers{*base_content};
    if (user_content) {
        layers.emplace_back(*user_content);
    }

    auto merged = gucc::profile::load_layered_net_profiles(layers);
    if (!merged) {
        spdlog::error("net profiles: failed to merge layered net profiles");
        return std::nullopt;
    }

    cached_url          = info.net_profs_url;
    cached_fallback_url = info.net_profs_fallback_url;
    cached_user_path    = info.net_profs_user_path;
    cached_content      = std::move(merged);
    return cached_content;
}

auto load_net_profs_content(const gucc::package::NetProfileInfo& info) noexcept -> std::optional<std::string> {
    // must have at least single valid net profile url
    if (info.net_profs_url.empty() && info.net_profs_fallback_url.empty()) {
        spdlog::error("Invalid netprofiles info: cannot be empty");
        return std::nullopt;
    }
    auto content = fetch_net_profiles_cached(info);
    if (!content) {
        spdlog::error("Failed to get net profiles content");
        return std::nullopt;
    }
    return content;
}

}  // namespace

namespace gucc::package {

auto get_pkglist_base(std::string_view packages, std::string_view root_filesystem, bool server_mode, NetProfileInfo net_profile_info) noexcept -> std::optional<std::vector<std::string>> {
    const auto& is_root_on_zfs      = (root_filesystem == "zfs"sv);
    const auto& is_root_on_btrfs    = (root_filesystem == "btrfs"sv);
    const auto& is_root_on_bcachefs = (root_filesystem == "bcachefs"sv);

    auto pkg_list = utils::make_multiline(packages, false, ' ');

    const auto pkg_count = pkg_list.size();
    for (std::size_t i = 0; i < pkg_count; ++i) {
        // NOTE: copy pkg instead of using reference
        const auto pkg = pkg_list[i];
        if (is_root_on_zfs && pkg.starts_with("linux-cachyos")) {
            pkg_list.emplace_back(fmt::format(FMT_COMPILE("{}-zfs"), pkg));
        }
        pkg_list.emplace_back(fmt::format(FMT_COMPILE("{}-headers"), pkg));
    }
    if (is_root_on_zfs) {
        pkg_list.insert(pkg_list.cend(), {"zfs-utils"});
    }
    if (is_root_on_bcachefs) {
        pkg_list.insert(pkg_list.cend(), {"bcachefs-tools"});
    }

    auto net_profs_content = load_net_profs_content(net_profile_info);
    if (!net_profs_content) {
        return std::nullopt;
    }
    auto base_net_profs = profile::parse_base_profiles(*net_profs_content);
    if (!base_net_profs.has_value()) {
        spdlog::error("Failed to parse net profiles");
        return std::nullopt;
    }

    if (server_mode == 0) {
        if (is_root_on_btrfs) {
            pkg_list.insert(pkg_list.cend(), {"snapper", "btrfs-assistant-git"});
        }
        pkg_list.insert(pkg_list.cend(),
            base_net_profs->base_desktop_packages.begin(),
            base_net_profs->base_desktop_packages.end());
    }
    pkg_list.insert(pkg_list.cend(),
        base_net_profs->base_packages.begin(),
        base_net_profs->base_packages.end());

    // Dynamically add CPU-specific microcode package
    const auto cpu_vendor = cpu::get_cpu_vendor();
    if (cpu_vendor == cpu::CpuVendor::Intel) {
        pkg_list.emplace_back("intel-ucode");
    } else if (cpu_vendor == cpu::CpuVendor::AMD) {
        pkg_list.emplace_back("amd-ucode");
    }

    return std::make_optional<std::vector<std::string>>(pkg_list);
}

auto get_pkglist_desktop(std::string_view desktop_env, NetProfileInfo net_profile_info) noexcept -> std::optional<std::vector<std::string>> {
    auto net_profs_content = load_net_profs_content(net_profile_info);
    if (!net_profs_content) {
        return std::nullopt;
    }
    auto desktop_net_profs = profile::parse_desktop_profiles(*net_profs_content);
    if (!desktop_net_profs.has_value()) {
        spdlog::error("Failed to parse net profiles");
        return std::nullopt;
    }

    std::vector<std::string> pkg_list{};

    constexpr std::string_view kde{"kde"};
    constexpr std::string_view xfce{"xfce"};
    constexpr std::string_view sway{"sway"};
    constexpr std::string_view wayfire{"wayfire"};
    constexpr std::string_view i3wm{"i3wm"};
    constexpr std::string_view gnome{"gnome"};
    constexpr std::string_view openbox{"openbox"};
    constexpr std::string_view bspwm{"bspwm"};
    constexpr std::string_view lxqt{"lxqt"};
    constexpr std::string_view cinnamon{"cinnamon"};
    constexpr std::string_view ukui{"ukui"};
    constexpr std::string_view qtile{"qtile"};
    constexpr std::string_view mate{"mate"};
    constexpr std::string_view lxde{"lxde"};
    constexpr std::string_view hyprland{"hyprland"};
    constexpr std::string_view budgie{"budgie"};

    bool needed_xorg{};
    auto found = std::ranges::search(desktop_env, i3wm);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, i3wm, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, sway);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, sway, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
    }
    found = std::ranges::search(desktop_env, kde);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, kde, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, xfce);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, xfce, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, gnome);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, gnome, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, wayfire);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, wayfire, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
    }
    found = std::ranges::search(desktop_env, openbox);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, openbox, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, lxqt);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, lxqt, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, bspwm);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, bspwm, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, cinnamon);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, cinnamon, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, ukui);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, ukui, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, qtile);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, qtile, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, mate);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, mate, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, lxde);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, lxde, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }
    found = std::ranges::search(desktop_env, hyprland);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, hyprland, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
    }
    found = std::ranges::search(desktop_env, budgie);
    if (!found.empty()) {
        auto profile = std::ranges::find(*desktop_net_profs, budgie, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
        needed_xorg = true;
    }

    if (needed_xorg) {
        auto profile = std::ranges::find(*desktop_net_profs, "xorg"sv, &profile::DesktopProfile::profile_name);
        pkg_list.insert(pkg_list.cend(), profile->packages.begin(), profile->packages.end());
    }

    return std::make_optional<std::vector<std::string>>(pkg_list);
}

auto get_netinstall_groups(NetProfileInfo net_profile_info) noexcept -> std::optional<std::vector<profile::NetinstallGroup>> {
    auto net_profs_content = load_net_profs_content(net_profile_info);
    if (!net_profs_content) {
        return std::nullopt;
    }
    return profile::parse_netinstall_groups(*net_profs_content);
}

auto get_servicelist_base(bool server_mode, NetProfileInfo net_profile_info) noexcept -> std::optional<std::vector<profile::ServiceEntry>> {
    auto net_profs_content = load_net_profs_content(net_profile_info);
    if (!net_profs_content) {
        return std::nullopt;
    }
    auto base_profs = profile::parse_base_profiles(*net_profs_content);
    if (!base_profs) {
        spdlog::error("Failed to parse net profiles");
        return std::nullopt;
    }

    // Skip sshd for desktop mode
    auto services = std::move(base_profs->base_services);
    if (!server_mode) {
        std::erase_if(services, [](const profile::ServiceEntry& entry) {
            return entry.name == "sshd";
        });
    }
    return std::make_optional(std::move(services));
}

auto get_servicelist_desktop(NetProfileInfo net_profile_info) noexcept -> std::optional<std::vector<profile::ServiceEntry>> {
    auto net_profs_content = load_net_profs_content(net_profile_info);
    if (!net_profs_content) {
        return std::nullopt;
    }
    auto base_profs = profile::parse_base_profiles(*net_profs_content);
    if (!base_profs) {
        spdlog::error("Failed to parse net profiles");
        return std::nullopt;
    }

    return std::make_optional(std::move(base_profs->base_desktop_services));
}

}  // namespace gucc::package
