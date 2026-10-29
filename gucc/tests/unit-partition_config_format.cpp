#include "doctest_compatibility.h"

#include "gucc/logger.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/testing/command_stub.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using gucc::fs::FilesystemType;
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

TEST_CASE("format partition")
{
    install_noop_logger();
    ScopedCommandStub stub;
    stub.on_prefix("mkfs."s, {});

    SECTION("vfat splits the mkfs flags and appends the device")
    {
        REQUIRE(gucc::fs::format_partition("/dev/x"sv, FilesystemType::Vfat));
        REQUIRE_EQ(stub.command_lines(), Lines{"mkfs.vfat -F32 /dev/x"});
        REQUIRE_EQ(stub.calls().front().argv, Lines{"mkfs.vfat", "-F32", "/dev/x"});
    }
    SECTION("btrfs")
    {
        REQUIRE(gucc::fs::format_partition("/dev/x"sv, FilesystemType::Btrfs));
        REQUIRE_EQ(stub.command_lines(), Lines{"mkfs.btrfs -f /dev/x"});
    }
    SECTION("unsupported FS")
    {
        const auto res = gucc::fs::format_partition("/dev/x"sv, FilesystemType::Zfs);
        REQUIRE_FALSE(res);
        REQUIRE_EQ(res.error().code, gucc::ErrorCode::Unsupported);
        REQUIRE(stub.command_lines().empty());
    }
    SECTION("unknown")
    {
        REQUIRE_FALSE(gucc::fs::format_partition("/dev/x"sv, FilesystemType::Unknown));
        REQUIRE(stub.command_lines().empty());
    }
}

TEST_CASE("format_partition propagates failing mkfs")
{
    install_noop_logger();
    ScopedCommandStub stub;
    stub.on_prefix("mkfs."s, CannedReply{.exit_code = 1});

    const auto res = gucc::fs::format_partition("/dev/x"sv, FilesystemType::Ext4);
    REQUIRE_FALSE(res);
    REQUIRE_EQ(res.error().code, gucc::ErrorCode::SubprocessFailed);
}
