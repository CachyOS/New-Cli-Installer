#include "gucc/package_profiles.hpp"
#include "detail/toml.hpp"

#include <algorithm>  // for contains
#include <ranges>     // for ranges::*
#include <sstream>    // for ostringstream

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

// the key we match array-of-tables group entries on
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

auto parse_toml(std::string_view content, std::string_view what) noexcept -> std::optional<toml::table> {
    toml::parse_result result = toml::parse(content);
    if (result.failed()) {
        spdlog::error("Failed to parse {}: {}", what, result.error().description());
        return std::nullopt;
    }
    return std::move(result).table();
}

auto base_profiles_from(const toml::table& netprof_table) noexcept -> gucc::profile::BaseProfiles {
    gucc::profile::BaseProfiles base_profiles{};
    parse_toml_array(netprof_table["base-packages"]["packages"].as_array(), base_profiles.base_packages);
    parse_toml_array(netprof_table["base-packages"]["desktop"]["packages"].as_array(), base_profiles.base_desktop_packages);
    parse_toml_service_array(netprof_table["services"]["units"].as_array(), base_profiles.base_services);
    parse_toml_service_array(netprof_table["services"]["desktop"]["units"].as_array(), base_profiles.base_desktop_services);
    return base_profiles;
}

auto desktop_profiles_from(const toml::table& netprof_table) noexcept -> std::vector<gucc::profile::DesktopProfile> {
    std::vector<gucc::profile::DesktopProfile> desktop_profiles{};
    parse_desktop_table(netprof_table["desktop"].as_table(), desktop_profiles);
    return desktop_profiles;
}

}  // namespace

namespace gucc::profile {

auto parse_base_profiles(std::string_view config_content) noexcept -> std::optional<BaseProfiles> {
    return parse_toml(config_content, "profiles"sv).transform(base_profiles_from);
}

auto parse_desktop_profiles(std::string_view config_content) noexcept -> std::optional<std::vector<DesktopProfile>> {
    return parse_toml(config_content, "profiles"sv).transform(desktop_profiles_from);
}

auto parse_net_profiles(std::string_view config_content) noexcept -> std::optional<NetProfiles> {
    return parse_toml(config_content, "profiles"sv).transform([](const toml::table& netprof_table) noexcept {
        return NetProfiles{
            .base_profiles    = base_profiles_from(netprof_table),
            .desktop_profiles = desktop_profiles_from(netprof_table),
        };
    });
}

auto parse_netinstall_groups(std::string_view config_content) noexcept -> std::optional<std::vector<NetinstallGroup>> {
    return parse_toml(config_content, "profiles"sv).transform([](const toml::table& netprof_table) noexcept {
        // simply no optional groups offered when the array is missing
        std::vector<NetinstallGroup> groups{};
        if (const auto* group_arr = netprof_table["netinstall"]["group"].as_array(); group_arr != nullptr) {
            for (const auto& node_el : *group_arr) {
                if (const auto* tbl = node_el.as_table(); tbl != nullptr) {
                    groups.emplace_back(parse_netinstall_group(*tbl));
                }
            }
        }
        return groups;
    });
}

auto merge_net_profiles(std::string_view lower, std::string_view higher) noexcept -> std::optional<std::string> {
    if (higher.empty()) {
        // nothing to merge
        return std::make_optional<std::string>(lower);
    }
    const auto higher_table = parse_toml(higher, "net profiles (hi)"sv);
    if (!higher_table) {
        return std::nullopt;
    }
    if (lower.empty()) {
        // lo doesn't exist
        return std::make_optional<std::string>(higher);
    }

    auto lower_table = parse_toml(lower, "net profiles (lo)"sv);
    if (!lower_table) {
        return std::nullopt;
    }
    merge_table(*lower_table, *higher_table);

    // overflow UB by default
    static constexpr auto kFlags = (toml::toml_formatter::default_flags | toml::format_flags::force_multiline_arrays)
        & ~(toml::format_flags::allow_literal_strings | toml::format_flags::allow_multi_line_strings);
    std::ostringstream oss;
    oss << toml::toml_formatter{*lower_table, kFlags};
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
