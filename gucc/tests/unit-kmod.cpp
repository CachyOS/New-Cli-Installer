#include "doctest_compatibility.h"

#include "gucc/kmod.hpp"
#include "gucc/logger.hpp"
#include "gucc/lvm.hpp"
#include "gucc/testing/command_stub.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

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

TEST_CASE("kmod")
{
    install_noop_logger();

    SECTION("load")
    {
        ScopedCommandStub stub;
        stub.on_prefix("/usr/bin/modprobe", {});
        REQUIRE(gucc::kmod::load({"zfs"sv, "dm-mod"sv}));
        CHECK_EQ(stub.command_lines(), std::vector<std::string>{"/usr/bin/modprobe -q -a zfs dm-mod"});
    }
    SECTION("unload")
    {
        ScopedCommandStub stub;
        stub.on_prefix("/usr/bin/modprobe", {});
        REQUIRE(gucc::kmod::unload({"zfs"sv}));
        CHECK_EQ(stub.command_lines(), std::vector<std::string>{"/usr/bin/modprobe -r -q -a zfs"});
    }
    SECTION("empty")
    {
        ScopedCommandStub stub;
        CHECK(gucc::kmod::load({}));
        CHECK(gucc::kmod::unload({}));
        CHECK(stub.command_lines().empty());
    }
    SECTION("failure")
    {
        ScopedCommandStub stub;
        stub.on_prefix("/usr/bin/modprobe", {.exit_code = 1});
        const auto res = gucc::kmod::load({"nope"sv});
        REQUIRE_FALSE(res);
        CHECK_EQ(res.error().code, gucc::ErrorCode::SubprocessFailed);
    }
}

TEST_CASE("activate lvm")
{
    install_noop_logger();

    ScopedCommandStub stub;
    stub.on([](const gucc::utils::InterceptedCommand&) { return true; }, {});
    REQUIRE(gucc::lvm::activate_lvm());
    CHECK_EQ(stub.command_lines(), std::vector<std::string>{"/usr/bin/modprobe -q -a dm-mod", "vgscan -v", "vgchange -ay -v"});
}
