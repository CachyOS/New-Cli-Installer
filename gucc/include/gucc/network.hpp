#ifndef NETWORK_HPP
#define NETWORK_HPP

#include "gucc/error.hpp"

#include <cstdint>      // for uint8_t
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::network {

/// Wi-Fi authentication scheme written into the [wifi-security] section of
/// a NetworkManager profile. Maps directly to NM's `key-mgmt` values per
/// nm-settings-nmcli(5).
enum class WifiSecurity : std::uint8_t {
    /// key-mgmt=wpa-psk. Per the NM docs, this covers both WPA2-Personal
    /// and WPA3-Personal./ i.e. WPA2/WPA3 transition-mode APs and
    /// WPA2-only APs both work with this value. Use this as the default
    /// choice for clients that just want to connect.
    WpaPsk,
    /// key-mgmt=sae. WPA3-Personal only. Required when an AP advertises
    /// SAE only (pure WPA3, no WPA2 transition). Requires NetworkManager
    /// 1.20+.
    Sae,
};

/// Static address configuration for one IP family. Drives a `method=manual`
/// [ipv4] or [ipv6] section instead of the DHCP `method=auto` default.
struct StaticIpConfig {
    /// Address in CIDR notation, e.g. "192.168.1.5/24" or
    /// "2001:db8::5/64". Required.
    std::string address;
    /// Default gateway. nullopt means no default route is configured by
    /// this profile.
    std::optional<std::string> gateway;
    /// DNS servers, written as a semicolon-separated `dns=` line. Leave
    /// empty to omit the line.
    std::vector<std::string> dns;
};

/// Optional knobs for the Ethernet profile builder. All members default
/// to "do nothing extra"; an empty config produces the DHCP/no-interface
/// profile shape that was previously emitted by make_ethernet_dhcp_profile.
struct EthernetConfig {
    /// Bind the profile to a specific interface. nullopt matches any
    /// Ethernet device.
    std::optional<std::string> interface_name;
    /// nullopt => `method=auto`. Set to opt in to a manual IPv4 address.
    std::optional<StaticIpConfig> ipv4_static;
    /// nullopt => `method=auto`. Set to opt in to a manual IPv6 address.
    std::optional<StaticIpConfig> ipv6_static;
};

/// Optional knobs for the Wi-Fi PSK profile builder. Defaults produce the
/// classic visible-SSID + DHCP-on-both-families profile.
struct WifiPskConfig {
    /// key-mgmt choice. See WifiSecurity for the doc-aligned semantics.
    WifiSecurity security{WifiSecurity::WpaPsk};
    /// When true, emit `hidden=true` in the [wifi] section so NM
    /// probe-scans the SSID instead of relying on broadcast.
    bool hidden{false};
    /// nullopt => `method=auto` for IPv4.
    std::optional<StaticIpConfig> ipv4_static;
    /// nullopt => `method=auto` for IPv6.
    std::optional<StaticIpConfig> ipv6_static;
};

/// Generate a v4-style UUID.
/// NetworkManager requires a UUID in every system connection profile.
[[nodiscard]] auto generate_uuid() noexcept -> std::string;

/// Build the body of an Ethernet .nmconnection profile. Defaults to DHCP
/// on both IPv4 and IPv6 and no interface binding; use @p config to set
/// an interface_name or static addresses.
[[nodiscard]] auto make_ethernet_profile(std::string_view id,
    std::string_view uuid,
    const EthernetConfig& config = {}) noexcept -> std::string;

/// Build the body of a PSK-authenticated Wi-Fi .nmconnection profile
/// (infrastructure mode). Defaults to WPA2/WPA3 transition (`key-mgmt=
/// wpa-psk`) with a visible SSID and DHCP on both IPv4 and IPv6. Use
/// @p config to opt into WPA3-only (Sae), hidden SSIDs, or static IPs.
/// PSK rules per nm-settings-nmcli(5): 8-63 ASCII chars or 64 hex chars
/// for wpa-psk; any length for sae. Validation is the caller's job.
[[nodiscard]] auto make_wifi_psk_profile(std::string_view id,
    std::string_view uuid,
    std::string_view ssid,
    std::string_view psk,
    const WifiPskConfig& config = {}) noexcept -> std::string;

/// Write a .nmconnection file at
/// <root>/etc/NetworkManager/system-connections/<filename>.
auto write_connection(std::string_view filename,
    std::string_view body,
    std::string_view root_mountpoint) noexcept -> Result<void>;

/// Copy every .nmconnection file from @p source_dir into the target's
/// NetworkManager system-connections directory. Each copied file is
/// chmod 0600. Returns the count of files copied, or -1 on I/O error.
/// A missing source directory is not an error and returns 0.
///
/// For example: copy the live ISO's NetworkManager state by
/// passing "/etc/NetworkManager/system-connections" as the source.
auto copy_connections_from(std::string_view source_dir,
    std::string_view root_mountpoint) noexcept -> std::int32_t;

}  // namespace gucc::network

#endif  // NETWORK_HPP
