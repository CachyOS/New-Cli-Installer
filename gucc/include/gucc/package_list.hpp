#ifndef PACKAGE_LIST_HPP
#define PACKAGE_LIST_HPP

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::package {

struct NetProfileInfo {
    // Url used to fetch netprofiles, e.g. 'file:///tmp/network-profile.toml'
    std::string net_profs_url;
    // Fallback url used to fetch netprofiles, e.g. 'file:///network-profile.toml'
    std::string net_profs_fallback_url;
};

// Get base profile packages
auto get_pkglist_base(std::string_view packages, std::string_view root_filesystem, bool server_mode, NetProfileInfo net_profile_info) noexcept -> std::optional<std::vector<std::string>>;

// Get desktop profile packages
auto get_pkglist_desktop(std::string_view desktop_env, NetProfileInfo net_profile_info) noexcept -> std::optional<std::vector<std::string>>;

}  // namespace gucc::package

#endif  // PACKAGE_LIST_HPP
