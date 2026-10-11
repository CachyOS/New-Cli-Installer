#include "doctest_compatibility.h"

#include "gucc/io_utils.hpp"
#include "gucc/logger.hpp"
#include "gucc/process.hpp"
#include "gucc/testing/command_stub.hpp"

#include <cstdint>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using gucc::utils::CommandInfo;
using gucc::utils::default_runner;
using gucc::utils::ExecutionObserver;
using gucc::utils::ProcessKind;
using gucc::utils::ProcessLocation;
using gucc::utils::ProcessResult;
using gucc::utils::RunOptions;
using gucc::utils::testing::ScopedCommandStub;
using gucc::utils::testing::Unmatched;

namespace io = gucc::utils;

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

class RecordingObserver final : public ExecutionObserver {
 public:
    void on_start(const CommandInfo& cmd) noexcept override {
        starts.push_back(cmd);
    }
    void on_line(const CommandInfo&, std::string_view line) noexcept override {
        lines.emplace_back(line);
    }
    void on_finish(const CommandInfo&, const ProcessResult& result) noexcept override {
        exit_codes.push_back(result.exit_code);
    }

    std::vector<CommandInfo> starts;
    std::vector<std::string> lines;
    std::vector<std::int32_t> exit_codes;
};

constexpr RunOptions kLoud{.kind = ProcessKind::Query};

}  // namespace

TEST_CASE("command stub")
{
  auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
      // noop
  });
  auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
  spdlog::set_default_logger(logger);
  gucc::logger::set_logger(logger);

  SECTION("exact and prefix rules")
  {
    ScopedCommandStub stub;
    stub.on_exact("uname -r"s, {.output = "7.2.0-cachyos\n"s})
        .on_prefix("lsblk "s, {.output = "sda\nsdb"s, .exit_code = 0})
        .on_prefix("false"s, {.output = "boom"s, .exit_code = 3});

    CHECK_EQ(io::exec("uname -r"sv), "7.2.0-cachyos"sv);
    CHECK_EQ(io::exec("lsblk -J /dev/sda"sv), "sda\nsdb"sv);
    CHECK_FALSE(io::exec_checked("false --really"sv));

    const auto res = default_runner().run_shell("false"sv, kLoud);
    CHECK_EQ(res.exit_code, 3);
    CHECK_EQ(res.output, "boom"sv);
    CHECK_FALSE(res.ok());

    CHECK_EQ(stub.command_lines(), std::vector{"uname -r"s, "lsblk -J /dev/sda"s, "false --really"s, "false"s});
    CHECK(stub.unmatched().empty());
    CHECK_EQ(stub.count_prefix("false"sv), 2);
  }
  SECTION("first registered wins")
  {
    ScopedCommandStub stub;
    stub.on_exact("zpool list"s, {.output = "specific"s})
        .on_prefix("zpool"s, {.output = "generic"s});

    CHECK_EQ(io::exec("zpool list"sv), "specific"sv);
    CHECK_EQ(io::exec("zpool status"sv), "generic"sv);
  }
  SECTION("unmatched fails with 127")
  {
    ScopedCommandStub stub;

    const auto res = default_runner().run_shell("rm -rf /definitely-not-run"sv);
    CHECK_EQ(res.exit_code, ScopedCommandStub::kUnmatchedExitCode);
    CHECK(res.output.empty());
    CHECK_EQ(stub.unmatched(), std::vector{"rm -rf /definitely-not-run"s});
    REQUIRE_EQ(stub.calls().size(), 1);
    CHECK_FALSE(stub.calls().front().matched);
  }
  SECTION("argv commands are joined and chroot prefix hidden")
  {
    ScopedCommandStub stub;
    stub.on_prefix("/usr/bin/zpool export"s, {});

    CHECK(io::exec_checked(std::vector{"/usr/bin/zpool"s, "export"s, "tank"s}));
    CHECK(io::arch_chroot_checked(std::vector{"/usr/bin/zpool"s, "export"s, "rpool"s}, "/mnt"sv));
    CHECK_FALSE(io::arch_chroot_checked("mkinitcpio -P"sv, "/mnt"sv));

    const auto calls = stub.calls();
    REQUIRE_EQ(calls.size(), 3);
    CHECK_EQ(calls[0].argv, std::vector{"/usr/bin/zpool"s, "export"s, "tank"s});
    CHECK_EQ(calls[0].location, ProcessLocation::Host);
    CHECK_EQ(calls[1].command_line, "/usr/bin/zpool export rpool"sv);
    CHECK_EQ(calls[1].location, ProcessLocation::Target);
    CHECK_EQ(calls[1].mountpoint, "/mnt"sv);
    CHECK_EQ(calls[2].command_line, "mkinitcpio -P"sv);
    CHECK_EQ(calls[2].argv, std::vector{"/bin/sh"s, "-c"s, "mkinitcpio -P"s});
  }
  SECTION("stdin and kind are recorded")
  {
    ScopedCommandStub stub;
    stub.on_prefix("chpasswd"s, {});

    CHECK(io::exec_checked_with_input("chpasswd"sv, "root:secret"sv));
    CHECK_EQ(io::exec("blkid"sv), ""sv);

    const auto calls = stub.calls();
    REQUIRE_EQ(calls.size(), 2);
    CHECK_EQ(calls[0].input, "root:secret"sv);
    CHECK_EQ(calls[0].kind, ProcessKind::Mutate);
    CHECK_EQ(calls[1].kind, ProcessKind::Query);
  }
  SECTION("observers see output as lines")
  {
    RecordingObserver observer;
    auto handle = default_runner().add_observer(observer);

    ScopedCommandStub stub;
    stub.on_exact("pacman -Syu"s, {.output = "one\r\ntwo\nthree\n"s, .exit_code = 1});

    const auto res = default_runner().run_shell("pacman -Syu"sv, kLoud);
    CHECK_EQ(res.output, "one\r\ntwo\nthree"sv);
    CHECK_EQ(res.exit_code, 1);

    REQUIRE_EQ(observer.starts.size(), 1);
    CHECK_EQ(observer.starts.front().display, "pacman -Syu"sv);
    CHECK_FALSE(observer.starts.front().skipped);
    CHECK_EQ(observer.lines, std::vector{"one"s, "two"s, "three"s});
    CHECK_EQ(observer.exit_codes, std::vector{1});

    observer.lines.clear();
    CHECK_EQ(io::exec("pacman -Syu"sv), "one\r\ntwo\nthree"sv);
    CHECK(observer.lines.empty());
  }
  SECTION("dry-run Mutate stays skipped")
  {
    RecordingObserver observer;
    auto handle = default_runner().add_observer(observer);

    ScopedCommandStub stub;
    stub.on_prefix("wipefs"s, {.output = "wiped"s, .exit_code = 9})
        .on_prefix("lsblk"s, {.output = "sda"s});

    default_runner().set_dry_run(true);
    const auto mutate = default_runner().run_shell("wipefs -a /dev/sda"sv);
    const auto query  = io::exec("lsblk"sv);
    default_runner().set_dry_run(false);

    CHECK(mutate.ok());
    CHECK(mutate.output.empty());
    CHECK_EQ(query, "sda"sv);
    CHECK_EQ(stub.command_lines(), std::vector{"lsblk"s});
    REQUIRE_EQ(observer.starts.size(), 2);
    CHECK(observer.starts[0].skipped);
    CHECK_FALSE(observer.starts[1].skipped);
  }
  SECTION("nesting delegates and restores")
  {
    ScopedCommandStub outer;
    outer.on_prefix("outer"s, {.output = "from outer"s});
    {
      ScopedCommandStub inner{Unmatched::Passthrough};
      inner.on_prefix("inner"s, {.output = "from inner"s});

      CHECK_EQ(io::exec("inner"sv), "from inner"sv);
      CHECK_EQ(io::exec("outer"sv), "from outer"sv);
      CHECK_EQ(inner.command_lines(), std::vector{"inner"s, "outer"s});
      CHECK_EQ(inner.unmatched(), std::vector{"outer"s});
    }
    {
      const ScopedCommandStub shadowing;
      CHECK_FALSE(io::query_checked("outer"sv));
    }
    CHECK_EQ(io::exec("outer"sv), "from outer"sv);
    CHECK_FALSE(io::query_checked("inner"sv));
    CHECK_EQ(outer.command_lines(), std::vector{"outer"s, "outer"s, "inner"s});
  }
  SECTION("destruction restores execution")
  {
    {
      ScopedCommandStub stub;
      stub.on_exact("echo real"s, {.output = "fake"s});
      CHECK_EQ(io::exec("echo real"sv), "fake"sv);
    }
    CHECK_EQ(io::exec("echo real"sv), "real"sv);
    CHECK_EQ(default_runner().run({"/bin/echo"s, "argv"s}, kLoud).output, "argv"sv);
  }
  SECTION("passthrough without stub runs")
  {
    ScopedCommandStub stub{Unmatched::Passthrough};
    stub.on_exact("echo real"s, {.output = "fake"s});

    CHECK_EQ(io::exec("echo real"sv), "fake"sv);
    CHECK_EQ(io::exec("echo passthrough"sv), "passthrough"sv);
  }
  SECTION("predicate rules see options")
  {
    ScopedCommandStub stub;
    stub.on([](const gucc::utils::InterceptedCommand& cmd) { return cmd.options.location == ProcessLocation::Target; },
        {.output = "in chroot"s});

    CHECK_EQ(io::arch_chroot_query("anything"sv, "/mnt"sv), "in chroot"sv);
    CHECK_FALSE(io::query_checked("anything"sv));
  }
}
