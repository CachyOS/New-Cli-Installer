#include "gucc/package_profiles.hpp"

#include <algorithm>  // for contains
#include <ranges>     // for ranges::*
#include <sstream>    // for ostringstream

#include <spdlog/spdlog.h>

#define TOML_EXCEPTIONS 0  // disable exceptions
#include <toml++/toml.h>

using namespace std::string_view_literals;

namespace {

// The identifying key used to match array-of-tables entries for groups.
inline constexpr auto GROUP_ID_KEY = "name"sv;

toml::array merge_array_of_tables(const toml::array& lower, const toml::array& higher) noexcept {
    const auto id_of = [](const toml::table& tbl) noexcept -> std::string_view {
        return tbl[GROUP_ID_KEY].value_or(""sv);
    };

    toml::array merged;
    std::vector<std::string_view> seen_ids{};

    for (const auto& node_el : lower) {
        const auto* lower_tbl = node_el.as_table();
        if (lower_tbl == nullptr) {
            merged.push_back(node_el);
            continue;
        }
        const auto id = id_of(*lower_tbl);
        // search for the same id in the hi
        const toml::table* replacement = nullptr;
        for (const auto& higher_el : higher) {
            const auto* higher_tbl = higher_el.as_table();
            if (higher_tbl != nullptr && !id.empty() && id_of(*higher_tbl) == id) {
                replacement = higher_tbl;
                break;
            }
        }
        merged.push_back(replacement != nullptr ? *replacement : *lower_tbl);
        if (!id.empty()) {
            seen_ids.push_back(id);
        }
    }

    // append from hi if id wasn't already present in lo
    for (const auto& higher_el : higher) {
        const auto* higher_tbl = higher_el.as_table();
        if (higher_tbl == nullptr) {
            merged.push_back(higher_el);
            continue;
        }
        const auto id = id_of(*higher_tbl);
        if (id.empty() || !std::ranges::contains(seen_ids, id)) {
            merged.push_back(*higher_tbl);
        }
    }

    return merged;
}

void merge_table(toml::table& lower, const toml::table& higher) noexcept {
    for (const auto& [key, higher_value] : higher) {
        auto* lower_value = lower.get(key);
        if (lower_value == nullptr) {
            lower.insert_or_assign(key, higher_value);
            continue;
        }

        // both have key-key
        if (lower_value->is_table() && higher_value.is_table()) {
            merge_table(*lower_value->as_table(), *higher_value.as_table());
            continue;
        }
        if (lower_value->is_array_of_tables() && higher_value.is_array_of_tables()) {
            auto merged = merge_array_of_tables(*lower_value->as_array(), *higher_value.as_array());
            lower.insert_or_assign(key, std::move(merged));
            continue;
        }
        // leaf replaces
        lower.insert_or_assign(key, higher_value);
    }
}

inline void parse_toml_array(const toml::array* arr, std::vector<std::string>& vec) noexcept {
    if (arr == nullptr) {
        return;
    }
    for (const auto& node_el : *arr) {
        auto elem = node_el.value<std::string_view>().value_or(""sv);
        if (!elem.empty()) {
            vec.emplace_back(elem);
        }
    }
}

void parse_desktop_table(const toml::table* desktop_table, std::vector<gucc::profile::DesktopProfile>& out) noexcept {
    if (desktop_table == nullptr) {
        return;
    }
    for (auto&& [key, value] : *desktop_table) {
        const auto* value_table = value.as_table();
        if (value_table == nullptr) {
            continue;
        }
        std::vector<std::string> packages{};
        parse_toml_array((*value_table)["packages"].as_array(), packages);
        out.emplace_back(gucc::profile::DesktopProfile{.profile_name = std::string{std::string_view{key}}, .packages = std::move(packages)});
    }
}

gucc::profile::NetinstallGroup parse_netinstall_group(const toml::table& tbl) noexcept {
    gucc::profile::NetinstallGroup group{};
    group.name        = std::string{tbl["name"].value_or(""sv)};
    group.description = std::string{tbl["description"].value_or(""sv)};
    group.icon        = std::string{tbl["icon"].value_or(""sv)};
    group.selected    = tbl["selected"].value_or(false);
    group.hidden      = tbl["hidden"].value_or(false);
    group.critical    = tbl["critical"].value_or(false);
    group.is_bundle   = tbl["bundle"].value_or(false);

    if (const auto* pkgs = tbl["packages"].as_array(); pkgs != nullptr) {
        parse_toml_array(pkgs, group.packages);
    }
    if (const auto* subs = tbl["subgroup"].as_array(); subs != nullptr) {
        for (const auto& node_el : *subs) {
            if (const auto* sub_tbl = node_el.as_table(); sub_tbl != nullptr) {
                group.subgroups.emplace_back(parse_netinstall_group(*sub_tbl));
            }
        }
    }
    return group;
}

inline void parse_toml_service_array(const toml::array* arr, std::vector<gucc::profile::ServiceEntry>& vec) noexcept {
    if (arr == nullptr) {
        return;
    }
    for (const auto& node_el : *arr) {
        const auto* tbl = node_el.as_table();
        if (tbl == nullptr) {
            continue;
        }
        auto name       = (*tbl)["name"].value<std::string_view>().value_or(""sv);
        auto action_str = (*tbl)["action"].value<std::string_view>().value_or("enable"sv);
        auto user       = (*tbl)["user"].value<bool>().value_or(false);
        auto urgent     = (*tbl)["urgent"].value<bool>().value_or(false);
        if (name.empty()) {
            continue;
        }
        vec.emplace_back(gucc::profile::ServiceEntry{
            .is_user_service = user,
            .is_urgent       = urgent,
            .action          = (action_str == "disable"sv) ? gucc::profile::ServiceAction::Disable : gucc::profile::ServiceAction::Enable,
            .name            = std::string{name},
        });
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
    parse_toml_service_array(netprof_table["services"]["units"].as_array(), base_profiles.base_services);
    parse_toml_service_array(netprof_table["services"]["desktop"]["units"].as_array(), base_profiles.base_desktop_services);
    return std::make_optional<BaseProfiles>(std::move(base_profiles));
}

auto parse_desktop_profiles(std::string_view config_content) noexcept -> std::optional<std::vector<DesktopProfile>> {
    toml::parse_result netprof = toml::parse(config_content);
    if (netprof.failed()) {
        spdlog::error("Failed to parse profiles: {}", netprof.error().description());
        return std::nullopt;
    }
    const auto& netprof_table = std::move(netprof).table();

    // simply no desktop profiles offered
    std::vector<DesktopProfile> desktop_profiles{};
    parse_desktop_table(netprof_table["desktop"].as_table(), desktop_profiles);
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

    // parse services
    parse_toml_service_array(netprof_table["services"]["units"].as_array(), net_profiles.base_profiles.base_services);
    parse_toml_service_array(netprof_table["services"]["desktop"]["units"].as_array(), net_profiles.base_profiles.base_desktop_services);

    // parse desktop
    parse_desktop_table(netprof_table["desktop"].as_table(), net_profiles.desktop_profiles);
    return std::make_optional<NetProfiles>(std::move(net_profiles));
}

auto parse_netinstall_groups(std::string_view config_content) noexcept -> std::optional<std::vector<NetinstallGroup>> {
    toml::parse_result netprof = toml::parse(config_content);
    if (netprof.failed()) {
        spdlog::error("Failed to parse profiles: {}", netprof.error().description());
        return std::nullopt;
    }
    const auto& netprof_table = std::move(netprof).table();

    std::vector<NetinstallGroup> groups{};

    // simply no optional groups offered.
    const auto* group_arr = netprof_table["netinstall"]["group"].as_array();
    if (group_arr == nullptr) {
        return std::make_optional<std::vector<NetinstallGroup>>(std::move(groups));
    }
    for (const auto& node_el : *group_arr) {
        if (const auto* tbl = node_el.as_table(); tbl != nullptr) {
            groups.emplace_back(parse_netinstall_group(*tbl));
        }
    }
    return std::make_optional<std::vector<NetinstallGroup>>(std::move(groups));
}

auto merge_net_profiles(std::string_view lower, std::string_view higher) noexcept -> std::optional<std::string> {
    if (higher.empty()) {
        // nothing to merge
        return std::make_optional<std::string>(lower);
    }
    if (lower.empty()) {
        // lo doesn't exist
        toml::parse_result higher_res = toml::parse(higher);
        if (higher_res.failed()) {
            spdlog::error("Failed to parse net profiles (hi): {}", higher_res.error().description());
            return std::nullopt;
        }
        return std::make_optional<std::string>(higher);
    }

    toml::parse_result lower_res = toml::parse(lower);
    if (lower_res.failed()) {
        spdlog::error("Failed to parse net profiles (lo): {}", lower_res.error().description());
        return std::nullopt;
    }
    toml::parse_result higher_res = toml::parse(higher);
    if (higher_res.failed()) {
        spdlog::error("Failed to parse net profiles (hi): {}", higher_res.error().description());
        return std::nullopt;
    }

    auto lower_table        = std::move(lower_res).table();
    const auto higher_table = std::move(higher_res).table();
    merge_table(lower_table, higher_table);

    std::ostringstream oss;
    oss << lower_table;
    return std::make_optional<std::string>(oss.str());
}

auto load_layered_net_profiles(const std::vector<std::string_view>& layers) noexcept -> std::optional<std::string> {
    std::optional<std::string> merged{};
    for (const auto& layer : layers) {
        if (layer.empty()) {
            continue;
        }
        auto next = merge_net_profiles(merged.value_or(std::string{}), layer);
        if (!next.has_value()) {
            spdlog::warn("Skipping unparsable/unmergeable net profiles layer");
            continue;
        }
        merged = std::move(next);
    }
    return merged;
}

}  // namespace gucc::profile
