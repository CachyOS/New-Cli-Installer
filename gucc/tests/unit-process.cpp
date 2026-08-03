#include "doctest_compatibility.h"

#include "gucc/logger.hpp"
#include "gucc/process.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using gucc::utils::LineAssembler;
using gucc::utils::ProcessRunner;
using gucc::utils::ProcessResult;
using gucc::utils::RunOptions;
using gucc::utils::ProcessKind;

using namespace std::chrono_literals;
using namespace std::string_view_literals;

namespace {
void install_noop_logger() {
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);
}
}  // namespace

TEST_CASE("process")
{
    SECTION("LineAssembler")
    {
        SECTION("incons out")
        {
            LineAssembler line_asm;
            std::vector<std::string> lines;
            const auto sink = [&](std::string_view line) { lines.emplace_back(line); };

            line_asm.feed("hello wo"sv, sink);
            REQUIRE(lines.empty());
            line_asm.feed("rld\nsecond"sv, sink);
            REQUIRE_EQ(lines.size(), 1);
            REQUIRE_EQ(lines[0], "hello world"sv);

            line_asm.flush(sink);
            REQUIRE_EQ(lines.size(), 2);
            REQUIRE_EQ(lines[1], "second"sv);
        }
        SECTION("multiline")
        {
            LineAssembler line_asm;
            std::vector<std::string> lines;
            line_asm.feed("a\nb\nc\n", [&](std::string_view line) { lines.emplace_back(line); });
            REQUIRE_EQ(lines.size(), 3);
            REQUIRE_EQ(lines[0], "a"sv);
            REQUIRE_EQ(lines[2], "c"sv);
        }
        SECTION("normalised")
        {
            LineAssembler line_asm;
            std::vector<std::string> lines;
            line_asm.feed("win\r\nline\r\n", [&](std::string_view line) { lines.emplace_back(line); });
            REQUIRE_EQ(lines.size(), 2);
            REQUIRE_EQ(lines[0], "win"sv);
            REQUIRE_EQ(lines[1], "line"sv);
        }
        SECTION("empty flush")
        {
            LineAssembler line_asm;
            std::vector<std::string> lines;
            line_asm.flush([&](std::string_view line) { lines.emplace_back(line); });
            REQUIRE(lines.empty());
        }
    }
    SECTION("ProcessRunner basic")
    {
        install_noop_logger();
        ProcessRunner runner;

        SECTION("trailing newline")
        {
            const std::vector<std::string> argv{"/usr/bin/echo", "hello"};
            const auto res = runner.run(argv);
            REQUIRE(res.ok());
            REQUIRE_EQ(res.exit_code, 0);
            REQUIRE_EQ(res.output, "hello"sv);
        }
        SECTION("exit is reported")
        {
            const auto res = runner.run_shell("exit 3");
            REQUIRE_EQ(res.exit_code, 3);
            REQUIRE(!res.ok());
        }
        SECTION("stdout and stderr")
        {
            const auto res = runner.run_shell("echo out; echo err 1>&2");
            REQUIRE(res.ok());
            REQUIRE(res.output.contains("out"));
            REQUIRE(res.output.contains("err"));
        }
        SECTION("LC_ALL set")
        {
            const auto res = runner.run_shell("printf 'LC=%s PATH_SET=%s' \"$LC_ALL\" \"${PATH:+yes}\"");
            REQUIRE(res.ok());
            REQUIRE(res.output.contains("LC=C"));
            REQUIRE(res.output.contains("PATH_SET=yes"));
        }
        SECTION("cancel before")
        {
            runner.cancel();
            REQUIRE(runner.cancelled());
            const auto res = runner.run({"/usr/bin/echo", "nope"});
            REQUIRE(res.cancelled);
            REQUIRE(!res.ok());
            runner.reset_cancel();
            REQUIRE(!runner.cancelled());
        }
        SECTION("cancel mid run")
        {
            ProcessResult res;
            const auto start = std::chrono::steady_clock::now();
            std::thread worker([&] { res = runner.run_shell("sleep 30"); });
            std::this_thread::sleep_for(150ms);
            runner.cancel();
            worker.join();
            const auto elapsed = std::chrono::steady_clock::now() - start;

            REQUIRE(res.cancelled);
            REQUIRE(!res.ok());
            REQUIRE(elapsed < 10s);
        }
    }
    SECTION("ProcessRunner dry run")
    {
        install_noop_logger();
        ProcessRunner runner;
        runner.set_dry_run(true);

        SECTION("mutate skipped with no output")
        {
            RunOptions opts{};
            opts.kind = ProcessKind::Mutate;
            const auto res = runner.run({"/usr/bin/echo", "should-not-run"}, opts);
            REQUIRE(res.ok());
            REQUIRE_EQ(res.exit_code, 0);
            REQUIRE(res.output.empty());
        }
        SECTION("commands default skipped")
        {
            const auto res = runner.run({"/usr/bin/echo", "should-not-run"});
            REQUIRE(res.ok());
            REQUIRE(res.output.empty());
        }
        SECTION("query still execute")
        {
            RunOptions opts{};
            opts.kind = ProcessKind::Query;
            const auto res = runner.run({"/usr/bin/echo", "read-me"}, opts);
            REQUIRE(res.ok());
            REQUIRE_EQ(res.output, "read-me"sv);
        }
    }
    SECTION("ProcessRunner secrets")
    {
        gucc::logger::clear_secrets();
        install_noop_logger();
        ProcessRunner runner;

        SECTION("registered secrets redacted")
        {
            std::string sink_text;
            runner.set_line_sink([&](std::string_view line) {
                sink_text.append(line);
                sink_text.push_back('\n');
            });
            gucc::logger::register_secret("abcdf");

            const auto res = runner.run_shell("echo pass=abcdf");
            REQUIRE(res.ok());
            REQUIRE_FALSE(sink_text.contains("abcdf"));
            REQUIRE(sink_text.contains("<redacted>"));
        }

        gucc::logger::clear_secrets();
    }
}
