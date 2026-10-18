#include "doctest_compatibility.h"

#include "gucc/error.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/logger.hpp"
#include "gucc/testing/command_stub.hpp"

#include <memory>       // for make_shared
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using gucc::ErrorCode;
using gucc::utils::ProcessLocation;
using gucc::utils::testing::ScopedCommandStub;

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST_CASE("or_error helpers")
{
  const auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) { });
  const auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
  spdlog::set_default_logger(logger);
  gucc::logger::set_logger(logger);
  gucc::logger::clear_secrets();

  SECTION("success run without error")
  {
    ScopedCommandStub stub;
    stub.on_exact("ufw default deny"s, {});

    const auto res = gucc::utils::arch_chroot_or_error("ufw default deny"sv, "/mnt"sv, "configure ufw"sv);
    REQUIRE(res.has_value());

    const auto calls = stub.calls();
    REQUIRE_EQ(calls.size(), 1);
    CHECK_EQ(calls.front().location, ProcessLocation::Target);
    CHECK_EQ(calls.front().mountpoint, "/mnt"s);
  }
  SECTION("chroot failure")
  {
    ScopedCommandStub stub;
    stub.on_exact("ufw default deny"s, {.exit_code = 3});

    const auto res = gucc::utils::arch_chroot_or_error("ufw default deny"sv, "/mnt"sv, "configure ufw"sv);
    REQUIRE_FALSE(res.has_value());
    CHECK_EQ(res.error().code, ErrorCode::SubprocessFailed);
    CHECK_EQ(res.error().context, "configure ufw: 'ufw default deny' in /mnt exited with 3"s);
  }
  SECTION("argv failure")
  {
    ScopedCommandStub stub;
    stub.on_exact("/usr/bin/zpool export tank"s, {.exit_code = 1});

    const auto res = gucc::utils::exec_or_error(std::vector{"/usr/bin/zpool"s, "export"s, "tank"s});
    REQUIRE_FALSE(res.has_value());
    CHECK_EQ(res.error().context, "'/usr/bin/zpool export tank' exited with 1"s);
  }
  SECTION("stdin never in the error")
  {
    ScopedCommandStub stub;
    stub.on_exact("cryptsetup open /dev/vda2 root"s, {.exit_code = 2});

    const auto res = gucc::utils::exec_or_error_with_input("cryptsetup open /dev/vda2 root"sv, "s3cr3t-pass\n"sv, "open luks partition"sv);
    REQUIRE_FALSE(res.has_value());
    CHECK_EQ(res.error().context, "open luks partition: 'cryptsetup open /dev/vda2 root' exited with 2"s);
    CHECK_FALSE(res.error().context.contains("s3cr3t-pass"sv));
    CHECK_EQ(stub.calls().front().input, "s3cr3t-pass\n"s);
  }
  SECTION("redacted secrets")
  {
    gucc::logger::register_secret("$6$hash");
    ScopedCommandStub stub;
    stub.on_prefix("/usr/bin/usermod"s, {.exit_code = 6});

    const auto res = gucc::utils::arch_chroot_or_error(std::vector{"/usr/bin/usermod"s, "-p"s, "$6$hash"s, "alice"s}, "/mnt"sv, "set user password"sv);
    REQUIRE_FALSE(res.has_value());
    CHECK_FALSE(res.error().context.contains("$6$hash"sv));
    CHECK(res.error().context.contains("<redacted>"sv));
    CHECK(res.error().context.ends_with(" in /mnt exited with 6"sv));
    gucc::logger::clear_secrets();
  }
}
