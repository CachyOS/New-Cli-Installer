#pragma once

#include "gucc/package_profiles.hpp"

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::package {

struct NetProfileInfo {
    // Url for the online copy of netprofiles (e.g. a web URL for the latest
    // upstream profiles).
    std::string net_profs_url;
    // Url for the vendored copy of netprofiles,
    // e.g. 'file:///var/lib/cachyos-installer/net-profiles.toml'.
    std::string net_profs_fallback_url;
    // Optional url to a user-provided netprofiles overlay merged on top of
    // the base document.
    std::string net_profs_user_path;
};

// Get base profile packages
auto get_pkglist_base(std::string_view packages, std::string_view root_filesystem, bool server_mode, NetProfileInfo net_profile_info) noexcept -> std::optional<std::vector<std::string>>;

// Get desktop profile packages
auto get_pkglist_desktop(std::string_view desktop_env, NetProfileInfo net_profile_info) noexcept -> std::optional<std::vector<std::string>>;

// Get netinstall groups from net profiles
auto get_netinstall_groups(NetProfileInfo net_profile_info) noexcept -> std::optional<std::vector<profile::NetinstallGroup>>;

// Get base service list from net profiles
auto get_servicelist_base(bool server_mode, NetProfileInfo net_profile_info) noexcept -> std::optional<std::vector<profile::ServiceEntry>>;

// Get desktop service list from net profiles
auto get_servicelist_desktop(NetProfileInfo net_profile_info) noexcept -> std::optional<std::vector<profile::ServiceEntry>>;

}  // namespace gucc::package
