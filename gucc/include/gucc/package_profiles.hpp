#pragma once

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

/// Groups may nest via `subgroups`.
struct NetinstallGroup {
    std::string name{};
    std::string description{};
    // symbol name for the GUI
    std::string icon{};
    bool selected{false};
    // never shown, installed when the parent is selected
    bool hidden{false};
    // locked-checked, cannot be unchecked
    bool critical{false};
    bool is_bundle{false};
    std::vector<std::string> packages{};
    std::vector<NetinstallGroup> subgroups{};
};

// Parse base profiles
auto parse_base_profiles(std::string_view config_content) noexcept -> std::optional<BaseProfiles>;

// Parse desktop profiles
auto parse_desktop_profiles(std::string_view config_content) noexcept -> std::optional<std::vector<DesktopProfile>>;

// Parse net profiles
auto parse_net_profiles(std::string_view config_content) noexcept -> std::optional<NetProfiles>;

// Parse optional netinstall groups.
auto parse_netinstall_groups(std::string_view config_content) noexcept -> std::optional<std::vector<NetinstallGroup>>;

/// Recursively merge two net-profiles TOML documents.
auto merge_net_profiles(std::string_view lower, std::string_view higher) noexcept -> std::optional<std::string>;

/// Fold an ordered list of net-profiles TOML sources into a single doc.
auto load_layered_net_profiles(const std::vector<std::string_view>& layers) noexcept -> std::optional<std::string>;

}  // namespace gucc::profile
