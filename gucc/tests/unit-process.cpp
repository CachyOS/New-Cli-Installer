#include "doctest_compatibility.h"

#include "gucc/logger.hpp"
#include "gucc/process.hpp"

#include <cstdint>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using gucc::utils::CommandInfo;
using gucc::utils::ExecutionObserver;
using gucc::utils::LineAssembler;
using gucc::utils::ProcessKind;
using gucc::utils::ProcessResult;
using gucc::utils::ProcessRunner;
using gucc::utils::RunOptions;

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

class RecordingObserver final : public ExecutionObserver {
 public:
    enum class Kind : std::uint8_t {
        Start,
        Line,
        Finish,
    };
    struct Event {
        Kind kind{};
        std::uint64_t id{};
        std::string display{};
        bool skipped{false};
        std::string line{};
        std::int32_t exit_code{-1};
    };

    void on_start(const CommandInfo& cmd) noexcept override {
        events.push_back({.kind = Kind::Start, .id = cmd.id, .display = cmd.display, .skipped = cmd.skipped});
    }
    void on_line(const CommandInfo& cmd, std::string_view line) noexcept override {
        events.push_back({.kind = Kind::Line, .id = cmd.id, .display = cmd.display, .skipped = cmd.skipped, .line = std::string{line}});
    }
    void on_finish(const CommandInfo& cmd, const ProcessResult& result) noexcept override {
        events.push_back({.kind = Kind::Finish, .id = cmd.id, .display = cmd.display, .skipped = cmd.skipped, .exit_code = result.exit_code});
    }

    [[nodiscard]] auto lines() const -> std::string {
        std::string text;
        for (const auto& event : events) {
            if (event.kind == Kind::Line) {
                text.append(event.line);
                text.push_back('\n');
            }
        }
        return text;
    }

    std::vector<Event> events;
};
using EventKind = RecordingObserver::Kind;
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

        SECTION("stdin input fed then closed")
        {
            RunOptions opts{};
            opts.input = "secret-line\n"sv;
            const auto res = runner.run({"/usr/bin/cat"}, opts);
            REQUIRE(res.ok());
            REQUIRE_EQ(res.output, "secret-line"sv);
        }
        SECTION("stdin closed without input")
        {
            const auto res = runner.run({"/usr/bin/cat"});
            REQUIRE(res.ok());
            REQUIRE(res.output.empty());
        }
        SECTION("extra env visible to child")
        {
            const std::vector<std::string> env{"COS_TEST_VAR=hello"};
            RunOptions opts{};
            opts.env = env;
            const auto res = runner.run_shell("printf '%s' \"$COS_TEST_VAR\"", opts);
            REQUIRE(res.ok());
            REQUIRE_EQ(res.output, "hello"sv);
        }
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
    SECTION("ProcessRunner observers")
    {
        install_noop_logger();
        ProcessRunner runner;

        SECTION("fanout to two observers")
        {
            RecordingObserver first;
            RecordingObserver second;
            const auto first_handle  = runner.add_observer(first);
            const auto second_handle = runner.add_observer(second);

            REQUIRE(runner.run({"/usr/bin/echo", "one"}).ok());
            REQUIRE(runner.run({"/usr/bin/echo", "two"}).ok());

            for (const auto* observer : {&first, &second}) {
                const auto& events = observer->events;
                REQUIRE_EQ(events.size(), 6);
                for (std::size_t cmd = 0; cmd < 2; ++cmd) {
                    const auto& start = events[cmd * 3];
                    const auto& line  = events[(cmd * 3) + 1];
                    const auto& fin   = events[(cmd * 3) + 2];
                    REQUIRE_EQ(start.kind, EventKind::Start);
                    REQUIRE_EQ(line.kind, EventKind::Line);
                    REQUIRE_EQ(fin.kind, EventKind::Finish);
                    REQUIRE_EQ(line.id, start.id);
                    REQUIRE_EQ(fin.id, start.id);
                    REQUIRE_EQ(fin.exit_code, 0);
                    REQUIRE_FALSE(start.skipped);
                }
                REQUIRE_EQ(events[1].line, "one"sv);
                REQUIRE_EQ(events[4].line, "two"sv);
                REQUIRE(events[3].id > events[0].id);
            }
            for (std::size_t i = 0; i < first.events.size(); ++i) {
                REQUIRE_EQ(first.events[i].id, second.events[i].id);
                REQUIRE_EQ(first.events[i].display, second.events[i].display);
            }
        }
        SECTION("handle detaches on destruction")
        {
            RecordingObserver observer;
            {
                const auto handle = runner.add_observer(observer);
                REQUIRE(runner.run({"/usr/bin/echo", "attached"}).ok());
            }
            const auto seen = observer.events.size();
            REQUIRE_EQ(seen, 3);

            REQUIRE(runner.run({"/usr/bin/echo", "detached"}).ok());
            REQUIRE_EQ(observer.events.size(), seen);
        }
        SECTION("handle reset detaches early")
        {
            RecordingObserver observer;
            auto handle = runner.add_observer(observer);
            handle.reset();

            REQUIRE(runner.run({"/usr/bin/echo", "detached"}).ok());
            REQUIRE(observer.events.empty());
        }
        SECTION("quiet commands get no on_line")
        {
            RecordingObserver observer;
            const auto handle = runner.add_observer(observer);

            const auto res = runner.run({"/usr/bin/echo", "hidden"}, RunOptions{.quiet = true, .kind = ProcessKind::Query});
            REQUIRE(res.ok());
            REQUIRE_EQ(res.output, "hidden"sv);

            REQUIRE_EQ(observer.events.size(), 2);
            REQUIRE_EQ(observer.events[0].kind, EventKind::Start);
            REQUIRE_EQ(observer.events[1].kind, EventKind::Finish);
        }
        SECTION("dry-run mutate reported as skipped")
        {
            RecordingObserver observer;
            const auto handle = runner.add_observer(observer);
            runner.set_dry_run(true);

            const auto res = runner.run({"/usr/bin/echo", "should-not-run"}, RunOptions{.kind = ProcessKind::Mutate});
            REQUIRE(res.ok());
            REQUIRE(res.output.empty());

            REQUIRE_EQ(observer.events.size(), 2);
            REQUIRE_EQ(observer.events[0].kind, EventKind::Start);
            REQUIRE_EQ(observer.events[1].kind, EventKind::Finish);
            REQUIRE(observer.events[0].skipped);
            REQUIRE(observer.events[1].skipped);
            REQUIRE_EQ(observer.events[1].exit_code, 0);
            REQUIRE_EQ(observer.events[0].id, observer.events[1].id);
            REQUIRE(observer.events[0].display.contains("should-not-run"));
        }
        SECTION("cancelled runner announces nothing")
        {
            RecordingObserver observer;
            const auto handle = runner.add_observer(observer);
            runner.cancel();

            REQUIRE(runner.run({"/usr/bin/echo", "nope"}).cancelled);
            REQUIRE(observer.events.empty());
            runner.reset_cancel();
        }
    }
    SECTION("ProcessRunner secrets")
    {
        gucc::logger::clear_secrets();
        install_noop_logger();
        ProcessRunner runner;

        SECTION("registered secrets redacted")
        {
            RecordingObserver observer;
            const auto handle = runner.add_observer(observer);
            gucc::logger::register_secret("abcdf");

            const auto res = runner.run({"/usr/bin/echo", "pass=abcdf"});
            REQUIRE(res.ok());

            const auto sink_text = observer.lines();
            REQUIRE_FALSE(sink_text.contains("abcdf"));
            REQUIRE(sink_text.contains("<redacted>"));

            REQUIRE_FALSE(observer.events.empty());
            for (const auto& event : observer.events) {
                REQUIRE_FALSE(event.display.contains("abcdf"));
                REQUIRE(event.display.contains("<redacted>"));
            }
        }

        gucc::logger::clear_secrets();
    }
}
