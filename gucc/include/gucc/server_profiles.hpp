#pragma once

#include "gucc/error.hpp"
#include "gucc/package_profiles.hpp"

#include <cstdint>  // for uint16_t, uint32_t

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::profile {

// NOTE(vnepogodin): should lock and updated with breaking changes
// to the installer config in order to maintain at least some compatibility

// Schema version.
inline constexpr std::uint32_t kServerSchemaVersion = 1;

struct ServerBaseline {
    std::vector<std::string> packages{};
    std::vector<ServiceEntry> services{};
    std::vector<std::uint16_t> firewall_tcp_ports{};
    std::vector<std::uint16_t> firewall_udp_ports{};
    std::vector<std::string> features{};
};

struct ServerProfile {
    std::string id{};
    std::string name{};
    std::string description{};
    std::vector<std::string> packages{};
    std::vector<ServiceEntry> services{};
    std::vector<std::uint16_t> firewall_tcp_ports{};
    std::vector<std::uint16_t> firewall_udp_ports{};
    std::vector<std::string> warnings{};
};

struct ServerProfiles {
    std::uint32_t schema_version{};
    std::string default_profile{};
    std::string default_kernel{};
    std::vector<std::string> profile_order{};
    ServerBaseline baseline{};
    std::vector<ServerProfile> profiles{};
};

// User-supplied
struct ServerUserExtras {
    std::vector<std::string> packages{};
    std::vector<std::uint16_t> firewall_tcp_ports{};
    std::vector<std::uint16_t> firewall_udp_ports{};
    std::vector<std::string> ssh_authorized_keys{};
};

struct ResolvedServerProfile {
    std::string id{};
    std::string name{};
    std::string description{};
    std::vector<std::string> packages{};
    std::vector<ServiceEntry> services{};
    std::vector<std::uint16_t> firewall_tcp_ports{};
    std::vector<std::uint16_t> firewall_udp_ports{};
    std::vector<std::string> warnings{};
    std::vector<std::string> features{};
    std::vector<std::string> ssh_authorized_keys{};
};

// Parse a server-profiles doc
auto parse_server_profiles(std::string_view config_content) noexcept -> Result<ServerProfiles>;

// Resolve one profile id into configuration
auto resolve_server_profile(const ServerProfiles& profiles, std::string_view profile_id, const ServerUserExtras& user_extras) noexcept -> Result<ResolvedServerProfile>;

// sshd_config.d drop-in
auto make_sshd_hardening_config() noexcept -> std::string;

}  // namespace gucc::profile
