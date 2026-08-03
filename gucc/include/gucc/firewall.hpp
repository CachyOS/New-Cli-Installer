#pragma once

#include "gucc/error.hpp"

#include <cstdint>      // for uint16_t
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::firewall {

// Applies the ufw configuration on the target
auto enable_ufw(std::string_view root_mountpoint, bool allow_kdeconnect) noexcept -> Result<void>;

// Build the ufw arguments
auto make_ufw_rules(const std::vector<std::uint16_t>& tcp_ports, const std::vector<std::uint16_t>& udp_ports) noexcept -> std::vector<std::string>;

// Configure and enable ufw for server use
auto configure_server_firewall(std::string_view root_mountpoint, const std::vector<std::uint16_t>& tcp_ports, const std::vector<std::uint16_t>& udp_ports) noexcept -> Result<void>;

}  // namespace gucc::firewall
