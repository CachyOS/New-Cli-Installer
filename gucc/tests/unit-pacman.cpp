#include "doctest_compatibility.h"

#include "gucc/logger.hpp"
#include "gucc/pacman.hpp"
#include "gucc/testing/command_stub.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using gucc::utils::ProcessKind;
using gucc::utils::ProcessLocation;
using gucc::utils::testing::ScopedCommandStub;

using namespace std::string_view_literals;

namespace {

void install_noop_logger() {
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) { });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);
}

}  // namespace

TEST_CASE("pacman is_installed")
{
    install_noop_logger();

    SECTION("host")
    {
        ScopedCommandStub stub;
        stub.on_exact("/usr/bin/pacman -Qq git", {});
        CHECK(gucc::pacman::is_installed("git"sv));
        CHECK_FALSE(gucc::pacman::is_installed("nope"sv));

        const auto calls = stub.calls();
        REQUIRE_EQ(calls.size(), 2);
        CHECK_EQ(calls[0].kind, ProcessKind::Query);
        CHECK_EQ(calls[0].location, ProcessLocation::Host);
    }
    SECTION("inside root")
    {
        ScopedCommandStub stub;
        stub.on_exact("/usr/bin/pacman -Qq git", {});
        CHECK(gucc::pacman::is_installed("git"sv, "/mnt"sv));

        const auto calls = stub.calls();
        REQUIRE_EQ(calls.size(), 1);
        CHECK_EQ(calls[0].kind, ProcessKind::Query);
        CHECK_EQ(calls[0].location, ProcessLocation::Target);
        CHECK_EQ(calls[0].mountpoint, "/mnt");
    }
}

TEST_CASE("pacman install")
{
    install_noop_logger();

    ScopedCommandStub stub;
    stub.on_prefix("/usr/bin/pacman -Sy", {});
    const std::vector<std::string> pkgs{"git", "vim"};
    REQUIRE(gucc::pacman::install(pkgs));
    REQUIRE(gucc::pacman::install({}));
    CHECK_EQ(stub.command_lines(), std::vector<std::string>{"/usr/bin/pacman -Sy --noconfirm git vim"});
}

TEST_CASE("pacman ensure_installed")
{
    install_noop_logger();

    SECTION("already installed skips install")
    {
        ScopedCommandStub stub;
        stub.on_exact("/usr/bin/pacman -Qq git", {});
        REQUIRE(gucc::pacman::ensure_installed("git"sv));
        CHECK_EQ(stub.command_lines(), std::vector<std::string>{"/usr/bin/pacman -Qq git"});
    }
    SECTION("missing gets installed")
    {
        ScopedCommandStub stub;
        stub.on_exact("/usr/bin/pacman -Qq git", {.exit_code = 1});
        stub.on_exact("/usr/bin/pacman -Sy --noconfirm git", {});
        REQUIRE(gucc::pacman::ensure_installed("git"sv));
        CHECK_EQ(stub.command_lines(), std::vector<std::string>{"/usr/bin/pacman -Qq git", "/usr/bin/pacman -Sy --noconfirm git"});
    }
    SECTION("install failure propagates")
    {
        ScopedCommandStub stub;
        stub.on_exact("/usr/bin/pacman -Qq git", {.exit_code = 1});
        stub.on_exact("/usr/bin/pacman -Sy --noconfirm git", {.exit_code = 1});
        const auto res = gucc::pacman::ensure_installed("git"sv);
        REQUIRE_FALSE(res);
        CHECK_EQ(res.error().code, gucc::ErrorCode::SubprocessFailed);
    }
}

TEST_CASE("pacman remove")
{
    install_noop_logger();

    ScopedCommandStub stub;
    stub.on_prefix("/usr/bin/pacman -Rsn", {});
    const std::vector<std::string> pkgs{"nano", "vi"};
    REQUIRE(gucc::pacman::remove(pkgs, "/mnt"sv));
    REQUIRE(gucc::pacman::remove({}, "/mnt"sv));

    const auto calls = stub.calls();
    REQUIRE_EQ(calls.size(), 1);
    CHECK_EQ(calls[0].command_line, "/usr/bin/pacman -Rsn --noconfirm nano vi");
    CHECK_EQ(calls[0].location, ProcessLocation::Target);
    CHECK_EQ(calls[0].mountpoint, "/mnt");
}
