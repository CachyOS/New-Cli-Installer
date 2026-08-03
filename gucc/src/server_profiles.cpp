#include "gucc/server_profiles.hpp"
#include "gucc/error.hpp"

#include <cstdint>  // for int64_t, uint16_t

#include <algorithm>  // for find, contains
#include <ranges>     // for ranges::*
#include <utility>    // for move

#include <fmt/compile.h>
#include <fmt/format.h>

#define TOML_EXCEPTIONS 0  // disable exceptions
#include <toml++/toml.h>

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
            return make_error(ErrorCode::InvalidArgument, fmt::format(FMT_COMPILE("invalid firewall port: {}"), node_el.value_or(""sv)));
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
            return make_error(ErrorCode::InvalidArgument, fmt::format(FMT_COMPILE("unknown service action '{}' for unit '{}'"), action_str, name));
        }
        if (std::ranges::find(out, name, &gucc::profile::ServiceEntry::name) != out.end()) {
            return make_error(ErrorCode::InvalidArgument, fmt::format(FMT_COMPILE("duplicate unit '{}' in server profile"), name));
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

}  // namespace

namespace gucc::profile {

auto parse_server_profiles(std::string_view config_content) noexcept -> Result<ServerProfiles> {
    toml::parse_result parsed = toml::parse(config_content);
    if (parsed.failed()) {
        return make_error(ErrorCode::ParseError, fmt::format(FMT_COMPILE("failed to parse server profiles: {}"), parsed.error().description()));
    }
    const auto& root = std::move(parsed).table();

    const auto* server = root["server"].as_table();
    if (server == nullptr) {
        return make_error(ErrorCode::ParseError, "server profiles document has no [server] table");
    }

    ServerProfiles result{};
    result.schema_version = static_cast<std::uint32_t>((*server)["schema_version"].value<std::int64_t>().value_or(0));
    if (result.schema_version != kServerSchemaVersion) {
        return make_error(ErrorCode::Unsupported, fmt::format(FMT_COMPILE("unsupported server schema_version {} (expected {})"), result.schema_version, kServerSchemaVersion));
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
                return make_error(ErrorCode::InvalidArgument, fmt::format(FMT_COMPILE("duplicate profile id '{}' in profile_order"), id));
            }
            result.profile_order.emplace_back(id);
        }
    }

    // [server.base]
    if (const auto* base = (*server)["base"].as_table(); base != nullptr) {
        auto packages = parse_packages((*base)["packages"].as_array());
        if (!packages) {
            return std::unexpected(std::move(packages.error()));
        }
        result.baseline.packages = std::move(*packages);

        auto services = parse_services_strict((*base)["units"].as_array());
        if (!services) {
            return std::unexpected(std::move(services.error()));
        }
        result.baseline.services = std::move(*services);

        auto tcp = parse_ports((*base)["firewall_tcp_ports"].as_array());
        if (!tcp) {
            return std::unexpected(std::move(tcp.error()));
        }
        result.baseline.firewall_tcp_ports = std::move(*tcp);

        auto udp = parse_ports((*base)["firewall_udp_ports"].as_array());
        if (!udp) {
            return std::unexpected(std::move(udp.error()));
        }
        result.baseline.firewall_udp_ports = std::move(*udp);

        auto features = parse_packages((*base)["features"].as_array());
        if (!features) {
            return std::unexpected(std::move(features.error()));
        }
        result.baseline.features = std::move(*features);
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

            auto packages = parse_packages((*prof_tbl)["packages"].as_array());
            if (!packages) {
                return std::unexpected(std::move(packages.error()));
            }
            profile.packages = std::move(*packages);

            auto services = parse_services_strict((*prof_tbl)["units"].as_array());
            if (!services) {
                return std::unexpected(std::move(services.error()));
            }
            profile.services = std::move(*services);

            auto tcp = parse_ports((*prof_tbl)["firewall_tcp_ports"].as_array());
            if (!tcp) {
                return std::unexpected(std::move(tcp.error()));
            }
            profile.firewall_tcp_ports = std::move(*tcp);

            auto udp = parse_ports((*prof_tbl)["firewall_udp_ports"].as_array());
            if (!udp) {
                return std::unexpected(std::move(udp.error()));
            }
            profile.firewall_udp_ports = std::move(*udp);

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
    const auto found = std::ranges::find(profiles.profiles, profile_id, &ServerProfile::id);
    if (found == profiles.profiles.end()) {
        return make_error(ErrorCode::NotFound, fmt::format(FMT_COMPILE("unknown server profile '{}'"), profile_id));
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
        auto existing = std::ranges::find(resolved.services, service.name, &ServiceEntry::name);
        if (existing != resolved.services.end()) {
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
