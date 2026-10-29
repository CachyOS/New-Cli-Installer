#include "doctest_compatibility.h"

#include "gucc/logger.hpp"
#include "gucc/repos.hpp"
#include "gucc/testing/command_stub.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using gucc::utils::ProcessLocation;
using gucc::utils::testing::RecordedCall;
using gucc::utils::testing::ScopedCommandStub;

using namespace std::string_view_literals;

namespace {

void install_noop_logger() {
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) { });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);
}

const std::vector<std::string> kKeyringSteps{
    "/usr/bin/pacman-key --init",
    "/usr/bin/pacman-key --populate archlinux cachyos",
    "/usr/bin/pacman-key --refresh-keys",
};

}  // namespace

TEST_CASE("reset keyrings")
{
    install_noop_logger();

    SECTION("basic")
    {
        ScopedCommandStub stub;
        stub.on_prefix("/usr/bin/pacman-key", {});
        REQUIRE(gucc::repos::reset_target_keyring("/mnt"sv));
        CHECK_EQ(stub.command_lines(), kKeyringSteps);

        const auto calls = stub.calls();
        CHECK(std::ranges::all_of(calls, [](ProcessLocation loc) { return loc == ProcessLocation::Target; }, &RecordedCall::location));
        CHECK(std::ranges::all_of(calls, [](std::string_view mnt) { return mnt == "/mnt"sv; }, &RecordedCall::mountpoint));
    }
    SECTION("refresh keys failed")
    {
        ScopedCommandStub stub;
        stub.on_exact(kKeyringSteps[2], {.exit_code = 2});
        stub.on_prefix("/usr/bin/pacman-key", {});
        CHECK(gucc::repos::reset_target_keyring("/mnt"sv));
        CHECK_EQ(stub.command_lines(), kKeyringSteps);
    }
    SECTION("fatal init failure")
    {
        ScopedCommandStub stub;
        stub.on_exact(kKeyringSteps[0], {.exit_code = 1});
        const auto res = gucc::repos::reset_target_keyring("/mnt"sv);
        REQUIRE_FALSE(res);
        CHECK_EQ(res.error().code, gucc::ErrorCode::SubprocessFailed);
        CHECK_EQ(stub.command_lines(), std::vector<std::string>{kKeyringSteps[0]});
    }
    SECTION("fatal populate failure")
    {
        ScopedCommandStub stub;
        stub.on_exact(kKeyringSteps[1], {.exit_code = 1});
        stub.on_prefix("/usr/bin/pacman-key", {});
        REQUIRE_FALSE(gucc::repos::reset_target_keyring("/mnt"sv));
        CHECK_EQ(stub.command_lines(), std::vector<std::string>{kKeyringSteps[0], kKeyringSteps[1]});
    }
}
