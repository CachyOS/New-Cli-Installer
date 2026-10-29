#include "doctest_compatibility.h"

#include "gucc/logger.hpp"
#include "gucc/swap.hpp"
#include "gucc/testing/command_stub.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using gucc::utils::testing::CannedReply;
using gucc::utils::testing::ScopedCommandStub;

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

void install_noop_logger() {
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) { });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);
}

using Lines = std::vector<std::string>;

}  // namespace

TEST_CASE("swapoff")
{
    install_noop_logger();

    SECTION("runs swapoff on the device")
    {
        ScopedCommandStub stub;
        stub.on_prefix("/usr/bin/swapoff "s, {});
        REQUIRE(gucc::swap::swapoff("/dev/sda3"sv));
        REQUIRE_EQ(stub.command_lines(), Lines{"/usr/bin/swapoff /dev/sda3"});
    }
    SECTION("failure propogates")
    {
        ScopedCommandStub stub;
        stub.on_prefix("/usr/bin/swapoff "s, CannedReply{.exit_code = 1});
        const auto res = gucc::swap::swapoff("/dev/sda3"sv);
        REQUIRE_FALSE(res);
        REQUIRE_EQ(res.error().code, gucc::ErrorCode::SubprocessFailed);
    }
}

TEST_CASE("make swap formats + activates")
{
    install_noop_logger();
    ScopedCommandStub stub;
    stub.on_prefix("/usr/bin/mkswap "s, {});
    stub.on_prefix("/usr/bin/swapon "s, {});

    REQUIRE(gucc::swap::make_swap_partition(gucc::fs::Partition{.fstype = "linuxswap"s, .mountpoint = ""s, .device = "/dev/sda3"s}));
    REQUIRE_EQ(stub.command_lines(), Lines{"/usr/bin/mkswap /dev/sda3", "/usr/bin/swapon /dev/sda3"});
}
