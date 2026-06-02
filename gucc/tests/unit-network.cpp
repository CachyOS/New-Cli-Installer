#include "doctest_compatibility.h"

#include "gucc/file_utils.hpp"
#include "gucc/logger.hpp"
#include "gucc/network.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

#include <fmt/format.h>
#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace {

class TempRoot final {
 public:
    TempRoot()
      : m_path(fs::temp_directory_path() / fs::path{fmt::format("gucc-network-{}", std::random_device{}())}) {
        fs::create_directories(m_path);
    }
    ~TempRoot() {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }
    TempRoot(const TempRoot&)                    = delete;
    auto operator=(const TempRoot&) -> TempRoot& = delete;
    TempRoot(TempRoot&&)                         = delete;
    auto operator=(TempRoot&&) -> TempRoot&      = delete;

    [[nodiscard]] auto path() const noexcept -> const fs::path& { return m_path; }

 private:
    fs::path m_path;
};

[[nodiscard]] auto looks_like_uuid(std::string_view s) -> bool {
    if (s.size() != 36) {
        return false;
    }
    constexpr std::array<std::size_t, 4> hyphens{8, 13, 18, 23};
    for (std::size_t i = 0; i < s.size(); ++i) {
        const bool expect_hyphen = std::ranges::contains(hyphens, i);
        if (expect_hyphen) {
            if (s[i] != '-') {
                return false;
            }
            continue;
        }
        const auto c = static_cast<unsigned char>(s[i]);
        if (std::isxdigit(c) == 0) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("network::generate_uuid")
{
    SECTION("shape and version")
    {
        const auto uuid = gucc::network::generate_uuid();
        REQUIRE(looks_like_uuid(uuid));
        REQUIRE_EQ(uuid[14], '4');
        const auto variant = uuid[19];
        REQUIRE((variant == '8' || variant == '9' || variant == 'a' || variant == 'b'));
    }
    SECTION("two calls produce distinct UUIDs")
    {
        const auto a = gucc::network::generate_uuid();
        const auto b = gucc::network::generate_uuid();
        REQUIRE_FALSE(a == b);
    }
}

TEST_CASE("network::make_ethernet_profile")
{
    constexpr auto kUuid = "11111111-2222-4333-8444-555555555555"sv;

    SECTION("default DHCP, no interface binding")
    {
        const auto body = gucc::network::make_ethernet_profile("Wired connection", kUuid);
        REQUIRE(body.contains("[connection]\n"sv));
        REQUIRE(body.contains("id=Wired connection\n"sv));
        REQUIRE(body.contains("uuid=11111111-2222-4333-8444-555555555555\n"sv));
        REQUIRE(body.contains("type=ethernet\n"sv));
        REQUIRE(body.contains("[ipv4]\nmethod=auto\n"sv));
        REQUIRE(body.contains("[ipv6]\nmethod=auto\n"sv));
        REQUIRE_FALSE(body.contains("interface-name="sv));
    }
    SECTION("interface-name binding")
    {
        const auto body = gucc::network::make_ethernet_profile("eth0", kUuid,
            gucc::network::EthernetConfig{.interface_name = "eth0"});
        REQUIRE(body.contains("interface-name=eth0\n"sv));
    }
    SECTION("static IPv4 with gateway and DNS")
    {
        const auto body = gucc::network::make_ethernet_profile("Wired", kUuid,
            gucc::network::EthernetConfig{
                .ipv4_static = gucc::network::StaticIpConfig{
                    .address = "192.168.1.5/24",
                    .gateway = "192.168.1.1",
                    .dns     = {"8.8.8.8", "1.1.1.1"},
                },
            });
        REQUIRE(body.contains("[ipv4]\nmethod=manual\n"sv));
        REQUIRE(body.contains("address1=192.168.1.5/24,192.168.1.1\n"sv));
        REQUIRE(body.contains("dns=8.8.8.8;1.1.1.1;\n"sv));
        REQUIRE(body.contains("[ipv6]\nmethod=auto\n"sv));
    }
    SECTION("static IPv4 without gateway omits the gateway clause")
    {
        const auto body = gucc::network::make_ethernet_profile("Wired", kUuid,
            gucc::network::EthernetConfig{
                .ipv4_static = gucc::network::StaticIpConfig{.address = "10.0.0.5/24"},
            });
        REQUIRE(body.contains("address1=10.0.0.5/24\n"sv));
        REQUIRE_FALSE(body.contains("address1=10.0.0.5/24,"sv));
        REQUIRE_FALSE(body.contains("dns="sv));
    }
    SECTION("static IPv6")
    {
        const auto body = gucc::network::make_ethernet_profile("Wired", kUuid,
            gucc::network::EthernetConfig{
                .ipv6_static = gucc::network::StaticIpConfig{
                    .address = "2001:db8::5/64",
                    .gateway = "2001:db8::1",
                },
            });
        REQUIRE(body.contains("[ipv6]\nmethod=manual\n"sv));
        REQUIRE(body.contains("address1=2001:db8::5/64,2001:db8::1\n"sv));
        REQUIRE(body.contains("[ipv4]\nmethod=auto\n"sv));
    }
}

TEST_CASE("network::make_wifi_psk_profile")
{
    constexpr auto kUuid = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"sv;

    SECTION("default WpaPsk (WPA2 + WPA3 transition per NM docs) emits key-mgmt=wpa-psk")
    {
        const auto body = gucc::network::make_wifi_psk_profile("Home", kUuid, "MyNet"sv, "secretpass"sv);
        REQUIRE(body.contains("type=wifi\n"sv));
        REQUIRE(body.contains("mode=infrastructure\n"sv));
        REQUIRE(body.contains("ssid=MyNet\n"sv));
        REQUIRE(body.contains("key-mgmt=wpa-psk\n"sv));
        REQUIRE(body.contains("psk=secretpass\n"sv));
        REQUIRE(body.contains("[ipv4]\nmethod=auto\n"sv));
        REQUIRE_FALSE(body.contains("hidden=true"sv));
    }
    SECTION("Sae (WPA3-only) emits key-mgmt=sae and nothing else")
    {
        const auto body = gucc::network::make_wifi_psk_profile("Home", kUuid, "MyNet"sv, "secretpass"sv,
            gucc::network::WifiPskConfig{.security = gucc::network::WifiSecurity::Sae});
        REQUIRE(body.contains("key-mgmt=sae\n"sv));
        REQUIRE_FALSE(body.contains("wpa-psk"sv));
    }
    SECTION("hidden=true emits hidden=true under [wifi]")
    {
        const auto body = gucc::network::make_wifi_psk_profile("Home", kUuid, "Stealth"sv, "secretpass"sv,
            gucc::network::WifiPskConfig{.hidden = true});
        REQUIRE(body.contains("ssid=Stealth\nhidden=true\n"sv));
    }
    SECTION("static IPv4 + IPv6 together")
    {
        const auto body = gucc::network::make_wifi_psk_profile("Home", kUuid, "MyNet"sv, "secretpass"sv,
            gucc::network::WifiPskConfig{
                .ipv4_static = gucc::network::StaticIpConfig{
                    .address = "192.168.1.10/24",
                    .gateway = "192.168.1.1",
                    .dns     = {"1.1.1.1"},
                },
                .ipv6_static = gucc::network::StaticIpConfig{
                    .address = "2001:db8::10/64",
                },
            });
        REQUIRE(body.contains("[ipv4]\nmethod=manual\n"sv));
        REQUIRE(body.contains("address1=192.168.1.10/24,192.168.1.1\n"sv));
        REQUIRE(body.contains("dns=1.1.1.1;\n"sv));
        REQUIRE(body.contains("[ipv6]\nmethod=manual\n"sv));
        REQUIRE(body.contains("address1=2001:db8::10/64\n"sv));
    }
}

TEST_CASE("network::write_connection")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {});
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("writes file with mode 0600 and the given body")
    {
        TempRoot root;
        const auto body = gucc::network::make_ethernet_profile("Wired", "11111111-2222-4333-8444-555555555555"sv);
        REQUIRE(gucc::network::write_connection("Wired.nmconnection"sv, body, root.path().string()));

        const auto written = root.path() / "etc" / "NetworkManager" / "system-connections" / "Wired.nmconnection";
        REQUIRE(fs::is_regular_file(written));
        REQUIRE_EQ(gucc::file_utils::read_whole_file(written.string()), body);
        const auto perms = fs::status(written).permissions();
        REQUIRE_EQ(perms, fs::perms::owner_read | fs::perms::owner_write);
    }
}

TEST_CASE("network::copy_connections_from")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {});
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("missing source returns 0 and writes nothing")
    {
        TempRoot root;
        REQUIRE_EQ(gucc::network::copy_connections_from(
            (root.path() / "missing").string(), root.path().string()), 0);
        REQUIRE_FALSE(fs::exists(root.path() / "etc" / "NetworkManager" / "system-connections"));
    }
    SECTION("copies only .nmconnection files and chmods 0600")
    {
        TempRoot root;
        const auto host = root.path() / "host" / "nm";
        fs::create_directories(host);
        std::ofstream{host / "Wired.nmconnection"} << "[connection]\nid=Wired\n";
        std::ofstream{host / "Wifi.nmconnection"} << "[connection]\nid=Wifi\n";
        std::ofstream{host / "README"} << "this is not a connection\n";

        const auto copied = gucc::network::copy_connections_from(host.string(), root.path().string());
        REQUIRE_EQ(copied, 2);

        const auto target = root.path() / "etc" / "NetworkManager" / "system-connections";
        REQUIRE(fs::is_regular_file(target / "Wired.nmconnection"));
        REQUIRE(fs::is_regular_file(target / "Wifi.nmconnection"));
        REQUIRE_FALSE(fs::exists(target / "README"));

        REQUIRE_EQ(fs::status(target / "Wired.nmconnection").permissions(),
            fs::perms::owner_read | fs::perms::owner_write);
    }
    SECTION("empty source returns 0 but creates the target dir")
    {
        TempRoot root;
        const auto host = root.path() / "host" / "nm";
        fs::create_directories(host);
        REQUIRE_EQ(gucc::network::copy_connections_from(host.string(), root.path().string()), 0);
        REQUIRE(fs::exists(root.path() / "etc" / "NetworkManager" / "system-connections"));
    }
}
