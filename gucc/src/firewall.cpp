#include "gucc/firewall.hpp"
#include "gucc/io_utils.hpp"

#include <cstdint>  // for uint16_t

#include <algorithm>    // for transform
#include <iterator>     // for back_inserter
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace {

auto run_ufw_commands(std::string_view root_mountpoint, const std::vector<std::string>& commands) noexcept -> gucc::Result<void> {
    for (const auto& command : commands) {
        if (!gucc::utils::arch_chroot_checked(command, root_mountpoint)) {
            return make_error(gucc::ErrorCode::SubprocessFailed, fmt::format(FMT_COMPILE("Failed to configure ufw on {}: {}"), root_mountpoint, command));
        }
    }
    return {};
}

}  // namespace

namespace gucc::firewall {

auto make_ufw_rules(const std::vector<std::uint16_t>& tcp_ports, const std::vector<std::uint16_t>& udp_ports) noexcept -> std::vector<std::string> {
    std::vector rules{
        "default deny incoming"s,
        "default allow outgoing"s,
    };
    std::ranges::transform(tcp_ports, std::back_inserter(rules), [](auto port) -> std::string {
        // TODO(vnepogodin): we should use non-default port
        // rate-limit SSH
        if (port == 22) {
            return "limit 22/tcp"s;
        }
        return fmt::format(FMT_COMPILE("allow {}/tcp"), port);
    });
    std::ranges::transform(udp_ports, std::back_inserter(rules), [](auto port) {
        return fmt::format(FMT_COMPILE("allow {}/udp"), port);
    });
    return rules;
}

auto configure_server_firewall(std::string_view root_mountpoint, const std::vector<std::uint16_t>& tcp_ports, const std::vector<std::uint16_t>& udp_ports) noexcept -> Result<void> {
    std::vector<std::string> commands{"systemctl enable ufw"s};
    const auto rules = make_ufw_rules(tcp_ports, udp_ports);
    std::ranges::transform(rules, std::back_inserter(commands), [](auto&& rule) {
        return fmt::format(FMT_COMPILE("ufw {}"), rule);
    });
    commands.emplace_back("ufw --force enable"s);

    if (auto res = run_ufw_commands(root_mountpoint, commands); !res) {
        return res;
    }
    spdlog::info("server ufw configured on {} ({} tcp, {} udp ports)", root_mountpoint, tcp_ports.size(), udp_ports.size());
    return {};
}

auto enable_ufw(std::string_view root_mountpoint, bool allow_kdeconnect) noexcept -> Result<void> {
    std::vector commands{
        "systemctl enable ufw"s,
        "ufw default deny incoming"s,
        "ufw default allow outgoing"s,
        "ufw enable"s,
    };
    // enable default rule to allow kde connect
    if (allow_kdeconnect) {
        commands.emplace_back(R"(ufw allow "KDE Connect")"s);
    }

    if (auto res = run_ufw_commands(root_mountpoint, commands); !res) {
        return res;
    }
    spdlog::info("ufw enabled on {} (kdeconnect={})", root_mountpoint, allow_kdeconnect);
    return {};
}

}  // namespace gucc::firewall
