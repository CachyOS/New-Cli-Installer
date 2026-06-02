#include "gucc/network.hpp"
#include "gucc/file_utils.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <random>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace {

using namespace gucc::network;

constexpr auto kSystemConnectionsRel = "etc/NetworkManager/system-connections"sv;
constexpr auto kConnectionExtension  = ".nmconnection"sv;

/// NM-required mode for system-connections files: 0600.
constexpr auto kConnectionMode = fs::perms::owner_read | fs::perms::owner_write;

constexpr auto key_mgmt_for(WifiSecurity security) noexcept -> std::string_view {
    switch (security) {
    case WifiSecurity::WpaPsk:
        return "wpa-psk"sv;
    case WifiSecurity::Sae:
        return "sae"sv;
    }
    return "wpa-psk"sv;
}

auto make_ip_section(std::string_view section_name,
    const std::optional<StaticIpConfig>& cfg) -> std::string {
    std::string out;
    out.reserve(96);
    out += fmt::format(FMT_COMPILE("\n[{}]\n"), section_name);
    if (!cfg) {
        out += "method=auto\n"sv;
        return out;
    }
    out += "method=manual\n"sv;
    if (cfg->gateway) {
        out += fmt::format(FMT_COMPILE("address1={},{}\n"), cfg->address, *cfg->gateway);
    } else {
        out += fmt::format(FMT_COMPILE("address1={}\n"), cfg->address);
    }
    if (!cfg->dns.empty()) {
        out += "dns="sv;
        std::ranges::for_each(cfg->dns, [&out](const std::string& d) {
            out += d;
            out += ';';
        });
        out += '\n';
    }
    return out;
}

}  // namespace

namespace gucc::network {

auto generate_uuid() noexcept -> std::string {
    // NOTE: maybe we should use C lib libuuid here instead of doing manual fuckery?
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<std::uint32_t> dist;
    std::array<std::uint32_t, 4> words{};
    std::ranges::generate(words, [&] { return dist(rng); });

    words[1] = (words[1] & 0xFFFF0FFFU) | 0x00004000U;
    words[2] = (words[2] & 0x3FFFFFFFU) | 0x80000000U;
    return fmt::format(
        FMT_COMPILE("{:08x}-{:04x}-{:04x}-{:04x}-{:04x}{:08x}"),
        words[0],
        (words[1] >> 16U) & 0xFFFFU,
        words[1] & 0xFFFFU,
        (words[2] >> 16U) & 0xFFFFU,
        words[2] & 0xFFFFU,
        words[3]);
}

auto make_ethernet_profile(std::string_view id,
    std::string_view uuid,
    const EthernetConfig& config) noexcept -> std::string {
    std::string body;
    body.reserve(256);
    body += "[connection]\n"sv;
    body += fmt::format(FMT_COMPILE("id={}\n"), id);
    body += fmt::format(FMT_COMPILE("uuid={}\n"), uuid);
    body += "type=ethernet\n"sv;
    body += "autoconnect=true\n"sv;
    if (config.interface_name) {
        body += fmt::format(FMT_COMPILE("interface-name={}\n"), *config.interface_name);
    }
    body += "\n[ethernet]\n"sv;
    body += make_ip_section("ipv4"sv, config.ipv4_static);
    body += make_ip_section("ipv6"sv, config.ipv6_static);
    return body;
}

auto make_wifi_psk_profile(std::string_view id,
    std::string_view uuid,
    std::string_view ssid,
    std::string_view psk,
    const WifiPskConfig& config) noexcept -> std::string {
    std::string body;
    body.reserve(320);
    body += "[connection]\n"sv;
    body += fmt::format(FMT_COMPILE("id={}\n"), id);
    body += fmt::format(FMT_COMPILE("uuid={}\n"), uuid);
    body += "type=wifi\n"sv;
    body += "autoconnect=true\n"sv;
    body += "\n[wifi]\nmode=infrastructure\n"sv;
    body += fmt::format(FMT_COMPILE("ssid={}\n"), ssid);
    if (config.hidden) {
        body += "hidden=true\n"sv;
    }
    body += "\n[wifi-security]\n"sv;
    body += fmt::format(FMT_COMPILE("key-mgmt={}\n"), key_mgmt_for(config.security));
    body += fmt::format(FMT_COMPILE("psk={}\n"), psk);
    body += make_ip_section("ipv4"sv, config.ipv4_static);
    body += make_ip_section("ipv6"sv, config.ipv6_static);
    return body;
}

auto write_connection(std::string_view filename,
    std::string_view body,
    std::string_view root_mountpoint) noexcept -> bool {
    const auto target_dir = fs::path{root_mountpoint} / kSystemConnectionsRel;
    std::error_code ec;
    fs::create_directories(target_dir, ec);
    if (ec) {
        spdlog::error("network: failed to create {}: {}", target_dir.string(), ec.message());
        return false;
    }
    const auto target_path = (target_dir / fs::path{filename}).string();
    if (!file_utils::create_file_for_overwrite(target_path, body)) {
        spdlog::error("network: failed to write {}", target_path);
        return false;
    }
    fs::permissions(target_path, kConnectionMode, fs::perm_options::replace, ec);
    if (ec) {
        spdlog::error("network: failed to set 0600 on {}: {}", target_path, ec.message());
        return false;
    }
    spdlog::info("network: connection written to {}", target_path);
    return true;
}

auto copy_connections_from(std::string_view source_dir,
    std::string_view root_mountpoint) noexcept -> int {
    std::error_code ec;
    if (!fs::is_directory(source_dir, ec)) {
        return 0;
    }

    const auto target_dir = fs::path{root_mountpoint} / kSystemConnectionsRel;
    fs::create_directories(target_dir, ec);
    if (ec) {
        spdlog::error("network: failed to create {}: {}", target_dir.string(), ec.message());
        return -1;
    }

    std::int32_t copied{};
    for (const auto& entry : fs::directory_iterator{source_dir, ec}) {
        if (ec) {
            spdlog::error("network: failed to iterate {}: {}", std::string{source_dir}, ec.message());
            return -1;
        }
        if (!entry.is_regular_file(ec) || entry.path().extension() != kConnectionExtension) {
            continue;
        }
        const auto dest = target_dir / entry.path().filename();
        fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            spdlog::error("network: failed to copy {}: {}", entry.path().string(), ec.message());
            return -1;
        }
        fs::permissions(dest, kConnectionMode, fs::perm_options::replace, ec);
        if (ec) {
            spdlog::warn("network: failed to set 0600 on {}: {}", dest.string(), ec.message());
        }
        ++copied;
    }
    spdlog::info("network: copied {} NetworkManager connections into {}", copied, target_dir.string());
    return copied;
}

}  // namespace gucc::network
