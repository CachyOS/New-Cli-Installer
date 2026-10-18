#include "gucc/server_profiles.hpp"
#include "detail/toml.hpp"
#include "gucc/error.hpp"
#include "gucc/ranges_utils.hpp"

#include <cstdint>  // for int64_t, uint16_t

#include <algorithm>  // for find, contains
#include <ranges>     // for ranges::*
#include <utility>    // for move

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_view_literals;

namespace {

using gucc::ErrorCode;
using gucc::make_error;

template <class T>
auto stable_unique(std::vector<T> in) noexcept -> std::vector<T> {
    std::vector<T> out{};
    out.reserve(in.size());
    for (auto& elem : in) {
        if (!std::ranges::contains(out, elem)) {
            out.emplace_back(std::move(elem));
        }
    }
    return out;
}

// glue a bunch of lists together
template <class T, class... Rs>
constexpr auto merge_stable_unique(Rs&&... ranges) -> std::vector<T> {
    return stable_unique(std::ranges::views::concat(std::forward<Rs>(ranges)...) | std::ranges::to<std::vector<T>>());
}

auto parse_packages(const toml::array* arr) noexcept -> gucc::Result<std::vector<std::string>> {
    std::vector<std::string> out{};
    if (arr == nullptr) {
        return out;
    }
    for (const auto& node_el : *arr) {
        const auto elem = node_el.value<std::string_view>().value_or(""sv);
        if (elem.empty()) {
            return make_error(ErrorCode::InvalidArgument, "empty package name in server profile");
        }
        out.emplace_back(elem);
    }
    return out;
}

// pull firewall ports, reject anything outside the valid range
auto parse_ports(const toml::array* arr) noexcept -> gucc::Result<std::vector<std::uint16_t>> {
    std::vector<std::uint16_t> out{};
    if (arr == nullptr) {
        return out;
    }
    for (const auto& node_el : *arr) {
        const auto value = node_el.value<std::int64_t>();
        if (!value || *value < 1 || *value > 65535) {
            return make_error(ErrorCode::InvalidArgument, FMT_COMPILE("invalid firewall port: {}"), node_el.value_or(""sv));
        }
        out.emplace_back(static_cast<std::uint16_t>(*value));
    }
    return out;
}

auto parse_services_strict(const toml::array* arr) noexcept -> gucc::Result<std::vector<gucc::profile::ServiceEntry>> {
    std::vector<gucc::profile::ServiceEntry> out{};
    if (arr == nullptr) {
        return out;
    }
    for (const auto& node_el : *arr) {
        const auto* tbl = node_el.as_table();
        if (tbl == nullptr) {
            continue;
        }
        const auto name = (*tbl)["name"].value<std::string_view>().value_or(""sv);
        if (name.empty()) {
            return make_error(ErrorCode::InvalidArgument, "server profile service unit missing name");
        }
        const auto action_str = (*tbl)["action"].value<std::string_view>().value_or(""sv);
        gucc::profile::ServiceAction action{};
        if (action_str == "enable"sv) {
            action = gucc::profile::ServiceAction::Enable;
        } else if (action_str == "disable"sv) {
            action = gucc::profile::ServiceAction::Disable;
        } else {
            return make_error(ErrorCode::InvalidArgument, FMT_COMPILE("unknown service action '{}' for unit '{}'"), action_str, name);
        }
        if (std::ranges::contains(out, name, &gucc::profile::ServiceEntry::name)) {
            return make_error(ErrorCode::InvalidArgument, FMT_COMPILE("duplicate unit '{}' in server profile"), name);
        }
        out.emplace_back(gucc::profile::ServiceEntry{
            .is_user_service = (*tbl)["user"].value<bool>().value_or(false),
            .is_urgent       = (*tbl)["urgent"].value<bool>().value_or(false),
            .action          = action,
            .name            = std::string{name},
        });
    }
    return out;
}

constexpr auto store_into(auto& field) noexcept {
    return [&field](auto&& value) noexcept { field = std::forward<decltype(value)>(value); };
}

template <class Target>
auto parse_common_lists(const toml::table& tbl, Target& out) noexcept -> gucc::Result<void> {
    const auto packages  = [&] { return parse_packages(tbl["packages"].as_array()).transform(store_into(out.packages)); };
    const auto services  = [&] { return parse_services_strict(tbl["units"].as_array()).transform(store_into(out.services)); };
    const auto tcp_ports = [&] { return parse_ports(tbl["firewall_tcp_ports"].as_array()).transform(store_into(out.firewall_tcp_ports)); };
    const auto udp_ports = [&] { return parse_ports(tbl["firewall_udp_ports"].as_array()).transform(store_into(out.firewall_udp_ports)); };
    return packages().and_then(services).and_then(tcp_ports).and_then(udp_ports);
}

}  // namespace

namespace gucc::profile {

auto parse_server_profiles(std::string_view config_content) noexcept -> Result<ServerProfiles> {
    toml::parse_result parsed = toml::parse(config_content);
    if (parsed.failed()) {
        return make_error(ErrorCode::ParseError, FMT_COMPILE("failed to parse server profiles: {}"), parsed.error().description());
    }
    const auto& root = std::move(parsed).table();

    const auto* server = root["server"].as_table();
    if (server == nullptr) {
        return make_error(ErrorCode::ParseError, "server profiles document has no [server] table");
    }

    ServerProfiles result{};
    result.schema_version = static_cast<std::uint32_t>((*server)["schema_version"].value<std::int64_t>().value_or(0));
    if (result.schema_version != kServerSchemaVersion) {
        return make_error(ErrorCode::Unsupported, FMT_COMPILE("unsupported server schema_version {} (expected {})"), result.schema_version, kServerSchemaVersion);
    }
    result.default_profile = std::string{(*server)["default_profile"].value_or(""sv)};
    result.default_kernel  = std::string{(*server)["default_kernel"].value_or(""sv)};

    // de-dup
    if (const auto* order = (*server)["profile_order"].as_array(); order != nullptr) {
        for (const auto& node_el : *order) {
            const auto id = node_el.value<std::string_view>().value_or(""sv);
            if (id.empty()) {
                continue;
            }
            if (std::ranges::contains(result.profile_order, std::string{id})) {
                return make_error(ErrorCode::InvalidArgument, FMT_COMPILE("duplicate profile id '{}' in profile_order"), id);
            }
            result.profile_order.emplace_back(id);
        }
    }

    // [server.base]
    if (const auto* base = (*server)["base"].as_table(); base != nullptr) {
        auto res = parse_common_lists(*base, result.baseline).and_then([&] {
            return parse_packages((*base)["features"].as_array()).transform(store_into(result.baseline.features));
        });
        if (!res) {
            return std::unexpected(std::move(res.error()));
        }
    }

    // [server.profiles.<id>]
    if (const auto* profiles = (*server)["profiles"].as_table(); profiles != nullptr) {
        for (auto&& [key, value] : *profiles) {
            const auto* prof_tbl = value.as_table();
            if (prof_tbl == nullptr) {
                continue;
            }
            ServerProfile profile{};
            profile.id          = std::string{std::string_view{key}};
            profile.name        = std::string{(*prof_tbl)["name"].value_or(""sv)};
            profile.description = std::string{(*prof_tbl)["description"].value_or(""sv)};

            if (auto res = parse_common_lists(*prof_tbl, profile); !res) {
                return std::unexpected(std::move(res.error()));
            }

            if (const auto* warnings = (*prof_tbl)["warnings"].as_array(); warnings != nullptr) {
                for (const auto& node_el : *warnings) {
                    const auto warn = node_el.value<std::string_view>().value_or(""sv);
                    if (!warn.empty()) {
                        profile.warnings.emplace_back(warn);
                    }
                }
            }

            result.profiles.emplace_back(std::move(profile));
        }
    }

    return result;
}

auto resolve_server_profile(const ServerProfiles& profiles, std::string_view profile_id, const ServerUserExtras& user_extras) noexcept -> Result<ResolvedServerProfile> {
    const auto* found = utils::find_ptr(profiles.profiles, profile_id, &ServerProfile::id);
    if (found == nullptr) {
        return make_error(ErrorCode::NotFound, FMT_COMPILE("unknown server profile '{}'"), profile_id);
    }
    const auto& profile = *found;

    ResolvedServerProfile resolved{};
    resolved.id          = profile.id;
    resolved.name        = profile.name;
    resolved.description = profile.description;

    // merged packages
    resolved.packages = merge_stable_unique<std::string>(profiles.baseline.packages, profile.packages, user_extras.packages);

    // merged services
    resolved.services = profiles.baseline.services;
    for (const auto& service : profile.services) {
        if (auto* existing = utils::find_ptr(resolved.services, service.name, &ServiceEntry::name)) {
            *existing = service;
        } else {
            resolved.services.emplace_back(service);
        }
    }

    // merged firewall ports
    resolved.firewall_tcp_ports = merge_stable_unique<std::uint16_t>(profiles.baseline.firewall_tcp_ports, profile.firewall_tcp_ports, user_extras.firewall_tcp_ports);
    resolved.firewall_udp_ports = merge_stable_unique<std::uint16_t>(profiles.baseline.firewall_udp_ports, profile.firewall_udp_ports, user_extras.firewall_udp_ports);

    resolved.warnings            = profile.warnings;
    resolved.features            = profiles.baseline.features;
    resolved.ssh_authorized_keys = stable_unique(user_extras.ssh_authorized_keys);
    resolved.default_kernel      = profiles.default_kernel;

    return resolved;
}

auto make_sshd_hardening_config() noexcept -> std::string {
    return std::string{
        "# Managed by CachyOS installer.\n"
        "PasswordAuthentication no\n"
        "KbdInteractiveAuthentication no\n"
        "PermitRootLogin no\n"};
}

}  // namespace gucc::profile
