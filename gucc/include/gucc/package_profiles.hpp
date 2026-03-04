#ifndef PACKAGE_PROFILES_HPP
#define PACKAGE_PROFILES_HPP

#include <cstdint>      // for uint8_t
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::profile {

enum class ServiceAction : std::uint8_t {
    Enable,
    Disable,
};

struct ServiceEntry {
    bool is_user_service{false};
    bool is_urgent{false};
    ServiceAction action{ServiceAction::Enable};
    std::string name;
};

struct BaseProfiles {
    std::vector<std::string> base_packages{};
    std::vector<std::string> base_desktop_packages{};
    std::vector<ServiceEntry> base_services{};
    std::vector<ServiceEntry> base_desktop_services{};
};

struct DesktopProfile {
    std::string profile_name{};
    std::vector<std::string> packages{};
};

struct NetProfiles {
    BaseProfiles base_profiles{};
    std::vector<DesktopProfile> desktop_profiles{};
};

// Parse base profiles
auto parse_base_profiles(std::string_view config_content) noexcept -> std::optional<BaseProfiles>;

// Parse desktop profiles
auto parse_desktop_profiles(std::string_view config_content) noexcept -> std::optional<std::vector<DesktopProfile>>;

// Parse net profiles
auto parse_net_profiles(std::string_view config_content) noexcept -> std::optional<NetProfiles>;

}  // namespace gucc::profile

#endif  // PACKAGE_PROFILES_HPP
