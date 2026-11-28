#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest_compatibility.h"

#include "cachyos/logging.hpp"
#include "cachyos/orchestrator.hpp"
#include "cachyos/session.hpp"

// import gucc
#include "gucc/process.hpp"

#include <cstddef>  // for ptrdiff_t

#include <algorithm>  // for contains, find, sort, adjacent_find
#include <iterator>   // for distance
#include <ranges>     // for ranges::*
#include <vector>     // for vector

using namespace std::string_view_literals;

using cachyos::installer::StepId;
using cachyos::installer::StepStatus;

namespace {

struct RecordingObserver final : cachyos::installer::InstallObserver {
    std::vector<StepId> begun;
    std::vector<std::pair<StepId, StepStatus>> ended;
    std::optional<StepId> cancel_on;
    cachyos::installer::InstallSession* session{};

    void on_step_begin(StepId step, [[maybe_unused]] std::size_t index, [[maybe_unused]] std::size_t total) noexcept override {
        begun.push_back(step);
        if (cancel_on == step) {
            session->cancel();
        }
    }
    void on_step_end(StepId step, StepStatus status, [[maybe_unused]] std::string_view detail) noexcept override {
        ended.emplace_back(step, status);
    }
};

auto position(const std::vector<StepId>& plan, StepId step) -> std::ptrdiff_t {
    const auto found = std::ranges::find(plan, step);
    REQUIRE(found != plan.end());
    return std::ranges::distance(plan.begin(), found);
}

}  // namespace

TEST_CASE("plan_steps")
{
    using cachyos::installer::plan_steps;
    cachyos::installer::InstallContext ctx{};

    SECTION("existing layout skips umount, cleanup is last")
    {
        const auto plan = plan_steps(ctx);
        REQUIRE_FALSE(std::ranges::contains(plan, StepId::Umount));
        REQUIRE_EQ(plan.front(), StepId::Partition);
        REQUIRE_EQ(plan.back(), StepId::Cleanup);
    }
    SECTION("server steps only with a resolved profile")
    {
        REQUIRE_FALSE(std::ranges::contains(plan_steps(ctx), StepId::ServerPackages));
        ctx.resolved_server = gucc::profile::ResolvedServerProfile{};
        const auto plan     = plan_steps(ctx);
        REQUIRE(std::ranges::contains(plan, StepId::ServerPackages));
        REQUIRE(std::ranges::contains(plan, StepId::SshKeys));
        REQUIRE(std::ranges::contains(plan, StepId::ServerFirewall));
    }
    SECTION("network carryover follows the flag")
    {
        ctx.carry_live_network = false;
        REQUIRE_FALSE(std::ranges::contains(plan_steps(ctx), StepId::NetworkCarryover));
    }
    SECTION("fstab comes right after base")
    {
        const auto plan = plan_steps(ctx);
        const auto base = std::ranges::find(plan, StepId::Base);
        REQUIRE(base != plan.end());
        REQUIRE_EQ(*std::next(base), StepId::Fstab);
    }
    SECTION("every step runs at most once")
    {
        ctx.resolved_server     = gucc::profile::ResolvedServerProfile{};
        ctx.post_install_script = "/root/post.sh";
        auto plan               = plan_steps(ctx);
        std::ranges::sort(plan);
        REQUIRE_EQ(std::ranges::adjacent_find(plan), plan.end());
    }
    SECTION("steps run after what they depend on")
    {
        ctx.resolved_server = gucc::profile::ResolvedServerProfile{};
        const auto plan     = plan_steps(ctx);
        const auto at       = [&plan](StepId step) { return position(plan, step); };
        // TODO(vnepogodin): we should add some real requirements such as via userver component register chain
        REQUIRE_LT(at(StepId::DetectCrypto), at(StepId::Base));
        REQUIRE_LT(at(StepId::Fstab), at(StepId::LuksKeyfile));
        REQUIRE_LT(at(StepId::LuksKeyfile), at(StepId::Initramfs));
        REQUIRE_EQ(at(StepId::Initramfs) + 1, at(StepId::RestoreInitramfsHook));
        REQUIRE_EQ(at(StepId::RestoreInitramfsHook) + 1, at(StepId::Bootloader));

        for (const auto step : {StepId::Desktop, StepId::Chwd, StepId::ServerPackages}) {
            REQUIRE_LT(at(step), at(StepId::Users));
        }
        REQUIRE_LT(at(StepId::Users), at(StepId::SshKeys));
        REQUIRE_LT(at(StepId::Users), at(StepId::Autologin));
    }
    SECTION("post-install only with a script, while the target is still mounted")
    {
        REQUIRE_FALSE(std::ranges::contains(plan_steps(ctx), StepId::PostInstall));
        ctx.post_install_script = "/root/post.sh";
        const auto plan         = plan_steps(ctx);
        REQUIRE_LT(position(plan, StepId::PostInstall), position(plan, StepId::Cleanup));
    }
}

TEST_CASE("run reports cancel to observers and stops")
{
    cachyos::installer::logging::init("/tmp/cos-test-orchestrator.log");
    auto& runner = gucc::utils::default_runner();
    runner.set_dry_run(true);

    cachyos::installer::InstallContext ctx{};
    ctx.mountpoint = "/nonexistent-cos-test-root";
    const cachyos::installer::SystemSettings sys{};
    const cachyos::installer::UserSettings user{};

    cachyos::installer::InstallSession session{runner};
    RecordingObserver observer;
    observer.session   = &session;
    observer.cancel_on = StepId::Base;
    const auto handle  = session.add_observer(observer);

    const auto result = cachyos::installer::run(ctx, sys, user, ""sv, session);
    REQUIRE_FALSE(result.success);
    REQUIRE_EQ(result.errors.size(), 1);
    REQUIRE_EQ(result.errors.front(), "Cancelled by user");

    REQUIRE_EQ(observer.begun.back(), StepId::Base);
    REQUIRE_FALSE(observer.ended.empty());
    REQUIRE_EQ(observer.ended.back().first, StepId::Base);
    REQUIRE_EQ(observer.ended.back().second, StepStatus::Cancelled);

    runner.reset_cancel();
}

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
