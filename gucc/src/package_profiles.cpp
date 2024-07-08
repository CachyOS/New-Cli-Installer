#include "gucc/package_profiles.hpp"

#include <spdlog/spdlog.h>

#define TOML_EXCEPTIONS 0  // disable exceptions
#include <toml++/toml.h>

using namespace std::string_view_literals;

namespace {

inline void parse_toml_array(const toml::array* arr, std::vector<std::string>& vec) noexcept {
    for (const auto& node_el : *arr) {
        auto elem = node_el.value<std::string_view>().value();
        vec.emplace_back(elem);
    }
}

}  // namespace

namespace gucc::profile {

auto parse_base_profiles(std::string_view config_content) noexcept -> std::optional<BaseProfiles> {
    toml::parse_result netprof = toml::parse(config_content);
    if (netprof.failed()) {
        spdlog::error("Failed to parse profiles: {}", netprof.error().description());
        return std::nullopt;
    }
    const auto& netprof_table = std::move(netprof).table();

    BaseProfiles base_profiles{};
    parse_toml_array(netprof_table["base-packages"]["packages"].as_array(), base_profiles.base_packages);
    parse_toml_array(netprof_table["base-packages"]["desktop"]["packages"].as_array(), base_profiles.base_desktop_packages);
    return std::make_optional<BaseProfiles>(std::move(base_profiles));
}

auto parse_desktop_profiles(std::string_view config_content) noexcept -> std::optional<std::vector<DesktopProfile>> {
    toml::parse_result netprof = toml::parse(config_content);
    if (netprof.failed()) {
        spdlog::error("Failed to parse profiles: {}", netprof.error().description());
        return std::nullopt;
    }
    const auto& netprof_table = std::move(netprof).table();

    std::vector<DesktopProfile> desktop_profiles{};

    auto desktop_table = netprof_table["desktop"].as_table();
    for (auto&& [key, value] : *desktop_table) {
        auto value_table = *value.as_table();
        std::vector<std::string> desktop_profile_packages{};
        parse_toml_array(value_table["packages"].as_array(), desktop_profile_packages);
        desktop_profiles.emplace_back(DesktopProfile{.profile_name = std::string{std::string_view{key}}, .packages = std::move(desktop_profile_packages)});
    }
    return std::make_optional<std::vector<DesktopProfile>>(std::move(desktop_profiles));
}

auto parse_net_profiles(std::string_view config_content) noexcept -> std::optional<NetProfiles> {
    toml::parse_result netprof = toml::parse(config_content);
    if (netprof.failed()) {
        spdlog::error("Failed to parse profiles: {}", netprof.error().description());
        return std::nullopt;
    }
    const auto& netprof_table = std::move(netprof).table();

    NetProfiles net_profiles{};

    // parse base
    parse_toml_array(netprof_table["base-packages"]["packages"].as_array(), net_profiles.base_profiles.base_packages);
    parse_toml_array(netprof_table["base-packages"]["desktop"]["packages"].as_array(), net_profiles.base_profiles.base_desktop_packages);

    // parse desktop
    auto desktop_table = netprof_table["desktop"].as_table();
    for (auto&& [key, value] : *desktop_table) {
        auto value_table = *value.as_table();
        std::vector<std::string> desktop_profile_packages{};
        parse_toml_array(value_table["packages"].as_array(), desktop_profile_packages);
        net_profiles.desktop_profiles.emplace_back(DesktopProfile{.profile_name = std::string{std::string_view{key}}, .packages = std::move(desktop_profile_packages)});
    }
    return std::make_optional<NetProfiles>(std::move(net_profiles));
}

}  // namespace gucc::profile
