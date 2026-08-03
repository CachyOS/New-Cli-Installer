#include "doctest_compatibility.h"

#include "gucc/firewall.hpp"
#include "gucc/logger.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

namespace {
auto has(const std::vector<std::string>& rules, std::string_view needle) -> bool {
    return std::ranges::contains(rules, std::string{needle});
}
}  // namespace

TEST_CASE("ufw")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {});
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("ssh")
    {
        const auto rules = gucc::firewall::make_ufw_rules({22, 80, 443}, {});
        CHECK(has(rules, "default deny incoming"));
        CHECK(has(rules, "default allow outgoing"));
        CHECK(has(rules, "limit 22/tcp"));
        CHECK(!has(rules, "allow 22/tcp"));
        CHECK(has(rules, "allow 80/tcp"));
        CHECK(has(rules, "allow 443/tcp"));
    }
    SECTION("minimal")
    {
        const auto rules = gucc::firewall::make_ufw_rules({22}, {});
        CHECK(has(rules, "default deny incoming"));
        CHECK(has(rules, "limit 22/tcp"));
        CHECK(!has(rules, "allow 80/tcp"));
        CHECK(!has(rules, "allow 443/tcp"));
    }
    SECTION("extra udp")
    {
        const auto rules = gucc::firewall::make_ufw_rules({22}, {51820});
        CHECK(has(rules, "allow 51820/udp"));
    }
    SECTION("defaults order")
    {
        const auto rules = gucc::firewall::make_ufw_rules({22, 9090}, {});
        REQUIRE(rules.size() >= 2);
        CHECK(rules[0] == "default deny incoming");
        CHECK(rules[1] == "default allow outgoing");
    }
}
