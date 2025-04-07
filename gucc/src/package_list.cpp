#include "gucc/package_list.hpp"
#include "gucc/fetch_file.hpp"
#include "gucc/fs_utils.hpp"
#include "gucc/package_profiles.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>  // for find, search
#include <ranges>     // for ranges::*

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::package {

auto get_pkglist_base(std::string_view packages, std::string_view root_filesystem, bool server_mode, NetProfileInfo net_profile_info) noexcept -> std::optional<std::vector<std::string>> {
    // must have at least single valid net profile url
    if (net_profile_info.net_profs_url.empty() && net_profile_info.net_profs_fallback_url.empty()) {
        spdlog::error("Invalid netprofiles info: cannot be empty");
        return std::nullopt;
    }

    const auto& is_root_on_zfs      = (root_filesystem == "zfs"sv);
    const auto& is_root_on_btrfs    = (root_filesystem == "btrfs"sv);
    const auto& is_root_on_bcachefs = (root_filesystem == "bcachefs"sv);

    auto pkg_list = utils::make_multiline(packages, false, ' ');

    const auto pkg_count = pkg_list.size();
    for (std::size_t i = 0; i < pkg_count; ++i) {
        const auto& pkg = pkg_list[i];
        pkg_list.emplace_back(fmt::format(FMT_COMPILE("{}-headers"), pkg));
        if (is_root_on_zfs && pkg.starts_with("linux-cachyos")) {
            pkg_list.emplace_back(fmt::format(FMT_COMPILE("{}-zfs"), pkg));
        }
    }
    if (is_root_on_zfs) {
        pkg_list.insert(pkg_list.cend(), {"zfs-utils"});
    }
    if (is_root_on_bcachefs) {
        pkg_list.insert(pkg_list.cend(), {"bcachefs-tools"});
    }

    auto net_profs_content = fetch::fetch_file_from_url(net_profile_info.net_profs_url, net_profile_info.net_profs_fallback_url);
    if (!net_profs_content) {
        spdlog::error("Failed to get net profiles content");
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

    return std::make_optional<std::vector<std::string>>(pkg_list);
}

auto get_pkglist_desktop(std::string_view desktop_env, NetProfileInfo net_profile_info) noexcept -> std::optional<std::vector<std::string>> {
    // must have at least single valid net profile url
    if (net_profile_info.net_profs_url.empty() && net_profile_info.net_profs_fallback_url.empty()) {
        spdlog::error("Invalid netprofiles info: cannot be empty");
        return std::nullopt;
    }

    auto net_profs_content = fetch::fetch_file_from_url(net_profile_info.net_profs_url, net_profile_info.net_profs_fallback_url);
    if (!net_profs_content) {
        spdlog::error("Failed to get net profiles content");
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

}  // namespace gucc::package
