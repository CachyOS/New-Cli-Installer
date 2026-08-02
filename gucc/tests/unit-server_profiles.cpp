#include "doctest_compatibility.h"

#include "gucc/logger.hpp"
#include "gucc/server_profiles.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

auto read_vendored() -> std::string {
    std::ifstream in{"server-profiles.toml"};
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

auto tcp_ports(const gucc::profile::ResolvedServerProfile& r) -> std::vector<std::uint16_t> {
    return r.firewall_tcp_ports;
}

static constexpr auto DOC_TEST = R"(
[server]
schema_version = 1
default_profile = "x"
profile_order = ["x"]

[server.base]
packages = ["base-pkg"]
units = [ { name = "foo", action = "enable" } ]
firewall_tcp_ports = [22]

[server.profiles.x]
name = "X"
units = [ { name = "foo", action = "disable" } ]
)"sv;

}  // namespace

TEST_CASE("server profiles")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("parse the vendored")
    {
        const auto content = read_vendored();
        REQUIRE(!content.empty());

        auto parsed = gucc::profile::parse_server_profiles(content);
        REQUIRE(parsed.has_value());
        CHECK_EQ(parsed->schema_version, gucc::profile::kServerSchemaVersion);
        CHECK_EQ(parsed->default_profile, "minimal");
        CHECK_EQ(parsed->default_kernel, "linux-cachyos-server");
        CHECK_EQ(parsed->profile_order, std::vector<std::string>{"minimal", "web", "container-host", "db", "cockpit"});

        CHECK(std::ranges::contains(parsed->baseline.packages, "cachyos-server-settings"));
        CHECK(std::ranges::contains(parsed->baseline.packages, "ufw"));
        CHECK_EQ(parsed->baseline.firewall_tcp_ports, std::vector<std::uint16_t>{22});
        CHECK(std::ranges::contains(parsed->baseline.features, "ufw-default-deny"));

        CHECK_EQ(parsed->profiles.size(), 5);
        for (const auto id : {"minimal"sv, "web"sv, "container-host"sv, "db"sv, "cockpit"sv}) {
            CAPTURE(id);
            CHECK(std::ranges::find(parsed->profiles, id, &gucc::profile::ServerProfile::id) != parsed->profiles.end());
        }

        auto db = std::ranges::find(parsed->profiles, "db"sv, &gucc::profile::ServerProfile::id);
        REQUIRE(db != parsed->profiles.end());
        REQUIRE_EQ(db->initializers.size(), 1);
        CHECK_EQ(db->initializers[0].kind, "postgresql");
        CHECK_EQ(db->initializers[0].data_dir, "/var/lib/postgres/data");
        CHECK_EQ(db->initializers[0].data_checksums, true);
        CHECK_EQ(db->initializers[0].auth_host, "scram-sha-256");
    }

    SECTION("merge baseline + profile + user extras")
    {
        auto parsed = gucc::profile::parse_server_profiles(read_vendored());
        REQUIRE(parsed.has_value());

        SECTION("minimal baseline")
        {
            auto resolved = gucc::profile::resolve_server_profile(*parsed, "minimal"sv, {});
            REQUIRE(resolved.has_value());
            CHECK(std::ranges::contains(resolved->packages, "cachyos-server-settings"));
            CHECK_EQ(resolved->firewall_tcp_ports, std::vector<std::uint16_t>{22});
            auto sshd = std::ranges::find(resolved->services, "sshd"sv, &gucc::profile::ServiceEntry::name);
            REQUIRE(sshd != resolved->services.end());
            CHECK_EQ(sshd->is_urgent, true);
        }
        SECTION("web adds ports 80/443 to the 22")
        {
            auto resolved = gucc::profile::resolve_server_profile(*parsed, "web"sv, {});
            REQUIRE(resolved.has_value());
            CHECK_EQ(tcp_ports(*resolved), std::vector<std::uint16_t>{22, 80, 443});
            CHECK(std::ranges::contains(resolved->packages, "nginx"));
        }
        SECTION("cockpit adds port 9090")
        {
            auto resolved = gucc::profile::resolve_server_profile(*parsed, "cockpit"sv, {});
            REQUIRE(resolved.has_value());
            CHECK_EQ(tcp_ports(*resolved), std::vector<std::uint16_t>{22, 9090});
            CHECK(!resolved->warnings.empty());
        }
        SECTION("de-duplicated user extras")
        {
            gucc::profile::ServerUserExtras extras{
                .packages            = {"htop", "nginx"},
                .firewall_tcp_ports  = {8080, 80},
                .firewall_udp_ports  = {},
                .ssh_authorized_keys = {"ssh-ed25519 AAAA admin@host"},
            };
            auto resolved = gucc::profile::resolve_server_profile(*parsed, "web"sv, extras);
            REQUIRE(resolved.has_value());
            CHECK_EQ(tcp_ports(*resolved), std::vector<std::uint16_t>{22, 80, 443, 8080});
            CHECK(std::ranges::contains(resolved->packages, "htop"));
            CHECK_EQ(std::ranges::count(resolved->packages, "nginx"), 1);
            CHECK_EQ(resolved->ssh_authorized_keys, std::vector<std::string>{"ssh-ed25519 AAAA admin@host"});
        }
        SECTION("unknown profile id is NotFound")
        {
            auto resolved = gucc::profile::resolve_server_profile(*parsed, "does-not-exist"sv, {});
            REQUIRE(!resolved.has_value());
            CHECK_EQ(resolved.error().code, gucc::ErrorCode::NotFound);
        }
    }
    SECTION("profile overrides action service")
    {
        auto parsed = gucc::profile::parse_server_profiles(DOC_TEST);
        REQUIRE(parsed.has_value());

        auto resolved = gucc::profile::resolve_server_profile(*parsed, "x"sv, {});
        REQUIRE(resolved.has_value());
        auto foo = std::ranges::find(resolved->services, "foo"sv, &gucc::profile::ServiceEntry::name);
        REQUIRE(foo != resolved->services.end());
        CHECK_EQ(foo->action, gucc::profile::ServiceAction::Disable);
        CHECK_EQ(std::ranges::count(resolved->services, "foo"sv, &gucc::profile::ServiceEntry::name), 1);
    }
    SECTION("invalids")
    {
        SECTION("missing [server] table")
        {
            auto parsed = gucc::profile::parse_server_profiles("[desktop.kde]\npackages = [\"x\"]\n"sv);
            REQUIRE(!parsed.has_value());
            CHECK_EQ(parsed.error().code, gucc::ErrorCode::ParseError);
        }
        SECTION("unsupported schema version")
        {
            auto parsed = gucc::profile::parse_server_profiles("[server]\nschema_version = 99\n"sv);
            REQUIRE(!parsed.has_value());
            CHECK_EQ(parsed.error().code, gucc::ErrorCode::Unsupported);
        }
        SECTION("duplicate id in profile_order")
        {
            auto parsed = gucc::profile::parse_server_profiles(R"(
[server]
schema_version = 1
profile_order = ["a", "a"]
)"sv);
            REQUIRE(!parsed.has_value());
            CHECK_EQ(parsed.error().code, gucc::ErrorCode::InvalidArgument);
        }
        SECTION("empty package name")
        {
            auto parsed = gucc::profile::parse_server_profiles(R"(
[server]
schema_version = 1
[server.base]
packages = ["ok", ""]
)"sv);
            REQUIRE(!parsed.has_value());
            CHECK_EQ(parsed.error().code, gucc::ErrorCode::InvalidArgument);
        }

        SECTION("unknown service action")
        {
            auto parsed = gucc::profile::parse_server_profiles(R"(
[server]
schema_version = 1
[server.base]
units = [ { name = "foo", action = "frobnicate" } ]
)"sv);
            REQUIRE(!parsed.has_value());
            CHECK_EQ(parsed.error().code, gucc::ErrorCode::InvalidArgument);
        }
        SECTION("duplicate unit within one profile")
        {
            auto parsed = gucc::profile::parse_server_profiles(R"(
[server]
schema_version = 1
[server.profiles.x]
name = "X"
units = [ { name = "foo", action = "enable" }, { name = "foo", action = "disable" } ]
)"sv);
            REQUIRE(!parsed.has_value());
            CHECK_EQ(parsed.error().code, gucc::ErrorCode::InvalidArgument);
        }
        SECTION("unknown initializer kind")
        {
            auto parsed = gucc::profile::parse_server_profiles(R"(
[server]
schema_version = 1
[server.profiles.x]
name = "X"
[[server.profiles.x.initializers]]
kind = "run-my-script"
)"sv);
            REQUIRE(!parsed.has_value());
            CHECK_EQ(parsed.error().code, gucc::ErrorCode::InvalidArgument);
        }
        SECTION("out-of-range firewall port")
        {
            auto parsed = gucc::profile::parse_server_profiles(R"(
[server]
schema_version = 1
[server.base]
firewall_tcp_ports = [70000]
)"sv);
            REQUIRE(!parsed.has_value());
            CHECK_EQ(parsed.error().code, gucc::ErrorCode::InvalidArgument);
        }
    }
    SECTION("config generators")
    {
        SECTION("networkd sshd")
        {
            CHECK(gucc::profile::make_networkd_dhcp_config().find("DHCP=yes") != std::string::npos);
            const auto sshd = gucc::profile::make_sshd_hardening_config();
            CHECK(sshd.find("PasswordAuthentication no") != std::string::npos);
            CHECK(sshd.find("PermitRootLogin no") != std::string::npos);
        }
        SECTION("psql args")
        {
            gucc::profile::ServerInitializer init{
                .kind           = "postgresql",
                .data_dir       = "/var/lib/postgres/data",
                .data_checksums = true,
                .auth_local     = "peer",
                .auth_host      = "scram-sha-256",
            };
            auto args = gucc::profile::make_postgresql_initdb_args(init);
            REQUIRE(args.has_value());
            CHECK(std::ranges::contains(*args, "initdb"));
            CHECK(std::ranges::contains(*args, "/var/lib/postgres/data"));
            CHECK(std::ranges::contains(*args, "--data-checksums"));
            CHECK(std::ranges::contains(*args, "--auth-host=scram-sha-256"));
        }
        SECTION("psql invalid")
        {
            CHECK(!gucc::profile::make_postgresql_initdb_args({.kind = "postgresql"}).has_value());
            CHECK(!gucc::profile::make_postgresql_initdb_args({.kind = "mysql", .data_dir = "/x"}).has_value());
        }
    }
}
