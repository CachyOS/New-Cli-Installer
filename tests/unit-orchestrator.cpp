#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest_compatibility.h"

#include "cachyos/orchestrator.hpp"

#include <algorithm>  // for ranges::contains, ranges::find

using namespace std::string_view_literals;

TEST_CASE("parse_pacman_progress")
{
    using cachyos::installer::parse_pacman_progress;

    SECTION("padded pacman install line")
    {
        const auto res = parse_pacman_progress("(  1/473) installing acl"sv);
        REQUIRE(res.has_value());
        CHECK(*res == doctest::Approx(1.0 / 473.0));
    }
    SECTION("compact form")
    {
        const auto res = parse_pacman_progress("(1/2)"sv);
        REQUIRE(res.has_value());
        CHECK(*res == doctest::Approx(0.5));
    }
    SECTION("completion: N == M is 1.0")
    {
        const auto res = parse_pacman_progress("(42/42) upgrading foo"sv);
        REQUIRE(res.has_value());
        CHECK(*res == doctest::Approx(1.0));
    }
    SECTION("first iteration is 0 when shown")
    {
        const auto res = parse_pacman_progress("(0/10) reading"sv);
        REQUIRE(res.has_value());
        CHECK(*res == doctest::Approx(0.0));
    }
    SECTION("plain text without parens returns nullopt")
    {
        CHECK_FALSE(parse_pacman_progress(":: Synchronizing package databases..."sv).has_value());
    }
    SECTION("missing slash returns nullopt")
    {
        CHECK_FALSE(parse_pacman_progress("(installing)"sv).has_value());
    }
    SECTION("missing closing paren returns nullopt")
    {
        CHECK_FALSE(parse_pacman_progress("(1/473 installing"sv).has_value());
    }
    SECTION("non-numeric content returns nullopt")
    {
        CHECK_FALSE(parse_pacman_progress("(one/two)"sv).has_value());
    }
    SECTION("zero denominator returns nullopt")
    {
        CHECK_FALSE(parse_pacman_progress("(0/0)"sv).has_value());
    }
    SECTION("N greater than M returns nullopt")
    {
        CHECK_FALSE(parse_pacman_progress("(10/5)"sv).has_value());
    }
    SECTION("empty line returns nullopt")
    {
        CHECK_FALSE(parse_pacman_progress(""sv).has_value());
    }
    SECTION("recognises the first marker on a line")
    {
        const auto res = parse_pacman_progress("noise (3/4) more noise"sv);
        REQUIRE(res.has_value());
        CHECK(*res == doctest::Approx(0.75));
    }
}

TEST_CASE("plan_steps additional packages")
{
    using cachyos::installer::StepId;
    cachyos::installer::InstallContext ctx{};

    SECTION("skipped without packages")
    {
        CHECK_FALSE(std::ranges::contains(cachyos::installer::plan_steps(ctx), StepId::AdditionalPackages));
    }
    SECTION("after the desktop packages and before users")
    {
        ctx.additional_packages = {"vlc"};
        const auto plan         = cachyos::installer::plan_steps(ctx);
        const auto at           = [&plan](StepId step) { return std::ranges::find(plan, step) - plan.begin(); };
        REQUIRE(std::ranges::contains(plan, StepId::AdditionalPackages));
        CHECK(at(StepId::Desktop) < at(StepId::AdditionalPackages));
        CHECK(at(StepId::AdditionalPackages) < at(StepId::Users));
    }
}
