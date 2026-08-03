#include "doctest_compatibility.h"

#include "gucc/logger.hpp"

#include "cachyos/packages.hpp"
#include "cachyos/types.hpp"

#include <algorithm>   // for contains, count
#include <filesystem>  // for current_path
#include <string>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_literals;

using cachyos::installer::init_server_profile;
using cachyos::installer::InstallContext;

namespace {

auto local_profiles_url() -> std::string {
    return "file://" + (std::filesystem::current_path() / "server-profiles.toml").string();
}

auto server_ctx(std::string profile_id) -> InstallContext {
    InstallContext ctx{};
    ctx.server_profile               = std::move(profile_id);
    ctx.server_profiles_url          = local_profiles_url();
    ctx.server_profiles_fallback_url = local_profiles_url();
    ctx.ssh_authorized_keys          = {"ssh-ed25519 AAAA admin@example.com"s};
    return ctx;
}

}  // namespace

TEST_CASE("init_server_profile") {
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) { });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("desktop leaves the profile empty") {
        InstallContext ctx{};
        const auto res = init_server_profile(ctx);

        CHECK(res.has_value());
        CHECK(!ctx.resolved_server.has_value());
    }
    SECTION("web profile resolves additively") {
        auto ctx                   = server_ctx("web"s);
        ctx.server_extra_packages  = {"htop"s};
        ctx.server_extra_tcp_ports = {8080};

        const auto res = init_server_profile(ctx);
        REQUIRE(res.has_value());
        REQUIRE(ctx.resolved_server.has_value());
        const auto& resolved = *ctx.resolved_server;

        CHECK_EQ(resolved.id, "web");

        CHECK(std::ranges::contains(resolved.packages, "openssh"s));
        CHECK(std::ranges::contains(resolved.packages, "nginx"s));
        CHECK(std::ranges::contains(resolved.packages, "certbot-nginx"s));
        CHECK(std::ranges::contains(resolved.packages, "htop"s));

        CHECK(std::ranges::contains(resolved.firewall_tcp_ports, std::uint16_t{22}));
        CHECK(std::ranges::contains(resolved.firewall_tcp_ports, std::uint16_t{80}));
        CHECK(std::ranges::contains(resolved.firewall_tcp_ports, std::uint16_t{443}));
        CHECK(std::ranges::contains(resolved.firewall_tcp_ports, std::uint16_t{8080}));

        const bool has_nginx = std::ranges::contains(resolved.services, "nginx"s, &gucc::profile::ServiceEntry::name);
        CHECK(has_nginx);

        CHECK_EQ(resolved.ssh_authorized_keys.size(), 1);
    }
    SECTION("unknown id aborts") {
        auto ctx       = server_ctx("does-not-exist"s);
        const auto res = init_server_profile(ctx);

        CHECK(!res.has_value());
        CHECK(!ctx.resolved_server.has_value());
    }
}
