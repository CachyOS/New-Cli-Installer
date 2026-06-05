#include "doctest_compatibility.h"

#include "gucc/logger.hpp"

#include "cachyos/requirements.hpp"

#include <algorithm>
#include <ranges>
#include <string_view>
#include <string>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

TEST_CASE("requirements")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    using cachyos::installer::check_requirements;
    using cachyos::installer::register_builtin_requirements;
    using cachyos::installer::register_requirement;
    using cachyos::installer::registered_requirement_ids;
    using cachyos::installer::Requirement;
    using cachyos::installer::RequirementsConfig;

    SECTION("registry round-trip with custom checker")
    {
        register_requirement("test_pass", [](const RequirementsConfig&) {
            Requirement r{};
            r.message_ok = "ok"sv;
            r.message_failed = "no"sv;
            r.satisfied = true;
            return r;
        });

        const auto ids = registered_requirement_ids();
        CHECK(std::ranges::find(ids, "test_pass"sv) != ids.end());

        RequirementsConfig cfg{};
        cfg.checks = {"test_pass"};
        cfg.mandatory = {};
        const auto results = check_requirements(cfg);
        REQUIRE_EQ(results.size(), 1);
        REQUIRE_EQ(results[0].id, "test_pass"sv);
        REQUIRE(results[0].satisfied);
        REQUIRE_FALSE(results[0].mandatory);
    }
    SECTION("mandatory flag is applied from config")
    {
        register_requirement("test_fail", [](const RequirementsConfig&) {
            return Requirement{
                .id = "test_fail",
                .message_ok = "ok",
                .message_failed = "no",
                .satisfied = false,
            };
        });

        RequirementsConfig cfg{};
        cfg.checks = {"test_fail"};
        cfg.mandatory = {"test_fail"};
        const auto results = check_requirements(cfg);
        REQUIRE_EQ(results.size(), 1);
        REQUIRE(results[0].mandatory);
        REQUIRE_FALSE(results[0].satisfied);

        const bool all_mandatory_satisfied = std::ranges::all_of(results,
            [](const Requirement& r) { return !r.mandatory || r.satisfied; });
        REQUIRE_FALSE(all_mandatory_satisfied);
    }
    SECTION("check order honors cfg.checks")
    {
        register_requirement("ord_a", [](const RequirementsConfig&) {
            return Requirement{.id = "ord_a", .satisfied = true};
        });
        register_requirement("ord_b", [](const RequirementsConfig&) {
            return Requirement{.id = "ord_b", .satisfied = true};
        });

        RequirementsConfig cfg{};
        cfg.mandatory = {};

        cfg.checks = {"ord_b", "ord_a"};
        auto results = check_requirements(cfg);
        REQUIRE_EQ(results.size(), 2);
        REQUIRE_EQ(results[0].id, "ord_b"sv);
        REQUIRE_EQ(results[1].id, "ord_a"sv);

        cfg.checks = {"ord_a", "ord_b"};
        results = check_requirements(cfg);
        REQUIRE_EQ(results.size(), 2);
        REQUIRE_EQ(results[0].id, "ord_a"sv);
        REQUIRE_EQ(results[1].id, "ord_b"sv);
    }
    SECTION("registered id overwrites whatever the checker returns")
    {
        register_requirement("real_id", [](const RequirementsConfig&) {
            Requirement r{};
            r.id = "lying_id";
            r.message_ok = "ok";
            r.message_failed = "no";
            r.satisfied = true;
            return r;
        });

        RequirementsConfig cfg{};
        cfg.checks = {"real_id"};
        cfg.mandatory = {};
        const auto results = check_requirements(cfg);
        REQUIRE_EQ(results.size(), 1);
        REQUIRE_EQ(results[0].id, "real_id"sv);
    }
    SECTION("unknown ids in cfg.checks are skipped, not thrown")
    {
        RequirementsConfig cfg{};
        cfg.checks = {"absolutely_not_a_real_check_id"};
        cfg.mandatory = {};
        const auto results = check_requirements(cfg);
        REQUIRE(results.empty());
    }
    SECTION("register_builtin_requirements")
    {
        register_builtin_requirements();
        const auto count_before =
            std::ranges::count(registered_requirement_ids(), "root"sv);
        register_builtin_requirements();
        register_builtin_requirements();
        const auto count_after =
            std::ranges::count(registered_requirement_ids(), "root"sv);
        REQUIRE_EQ(count_before, 1);
        REQUIRE_EQ(count_after, count_before);

        RequirementsConfig cfg{};
        cfg.checks = {"root"};
        cfg.mandatory = {"root"};
        const auto results = check_requirements(cfg);
        REQUIRE_EQ(results.size(), 1);
        REQUIRE_EQ(results[0].id, "root"sv);
        REQUIRE_FALSE(results[0].message_ok.empty());
        REQUIRE_FALSE(results[0].message_failed.empty());
        REQUIRE(results[0].mandatory);
    }
    SECTION("storage checker reports thresholds in its messages")
    {
        register_builtin_requirements();
        RequirementsConfig cfg{};
        cfg.required_storage_gib = 8.0;
        cfg.checks = {"storage"};
        cfg.mandatory = {};
        const auto results = check_requirements(cfg);
        REQUIRE_EQ(results.size(), 1);
        REQUIRE_EQ(results[0].id, "storage"sv);
        REQUIRE(results[0].message_ok.contains("8.0"sv));
        REQUIRE(results[0].message_failed.contains("8.0"sv));
    }
}
