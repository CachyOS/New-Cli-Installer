#pragma once

#include "gucc/process.hpp"

#include <cstddef>  // for size_t
#include <cstdint>  // for int32_t, uint8_t

#include <algorithm>    // for find_if
#include <concepts>     // for predicate
#include <functional>   // for function
#include <mutex>        // for mutex, scoped_lock
#include <optional>     // for optional
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for move
#include <vector>       // for vector

namespace gucc::utils::testing {

// I like spam
struct CannedReply {
    std::string output{};
    std::int32_t exit_code{0};
};

struct RecordedCall {
    std::string command_line{};
    std::vector<std::string> argv{};
    ProcessKind kind{ProcessKind::Mutate};
    ProcessLocation location{ProcessLocation::Host};
    std::string mountpoint{};
    std::string input{};
    bool matched{false};
};

enum class Unmatched : std::uint8_t {
    /// Record and answer with exit 127, empty output.
    Fail,
    /// Delegate to the enclosing stub, or run if there is none.
    Passthrough,
};

/// @brief Installs a rule table on the runner for its lifetime.
///
/// Rules match on command_line. The first registered
/// match wins. Stubs must be destroyed in LIFO order.
class [[nodiscard]] ScopedCommandStub final {
 public:
    using Predicate = std::function<bool(const InterceptedCommand&)>;

    static constexpr std::int32_t kUnmatchedExitCode = 127;

    explicit ScopedCommandStub(Unmatched policy = Unmatched::Fail)
      : m_policy(policy),
        m_previous(default_runner().set_test_interceptor([this](const InterceptedCommand& cmd) { return handle(cmd); })) { }

    ~ScopedCommandStub() {
        default_runner().set_test_interceptor(std::move(m_previous));
    }

    ScopedCommandStub(const ScopedCommandStub&)                    = delete;
    ScopedCommandStub(ScopedCommandStub&&)                         = delete;
    auto operator=(const ScopedCommandStub&) -> ScopedCommandStub& = delete;
    auto operator=(ScopedCommandStub&&) -> ScopedCommandStub&      = delete;

    auto on(Predicate matches, CannedReply reply) -> ScopedCommandStub& {
        const std::scoped_lock lock(m_mutex);
        m_rules.emplace_back(std::move(matches), std::move(reply));
        return *this;
    }

    auto on_exact(std::string command_line, CannedReply reply) -> ScopedCommandStub& {
        return on([expected = std::move(command_line)](const InterceptedCommand& cmd) { return cmd.command_line == expected; },
            std::move(reply));
    }

    auto on_prefix(std::string prefix, CannedReply reply) -> ScopedCommandStub& {
        return on([expected = std::move(prefix)](const InterceptedCommand& cmd) { return cmd.command_line.starts_with(expected); },
            std::move(reply));
    }

    [[nodiscard]] auto calls() const -> std::vector<RecordedCall> {
        const std::scoped_lock lock(m_mutex);
        return m_calls;
    }

    [[nodiscard]] auto command_lines() const -> std::vector<std::string> {
        return command_lines_where([](const RecordedCall&) { return true; });
    }

    [[nodiscard]] auto unmatched() const -> std::vector<std::string> {
        return command_lines_where([](const RecordedCall& call) { return !call.matched; });
    }

    [[nodiscard]] auto count_prefix(std::string_view prefix) const -> std::size_t {
        return command_lines_where([prefix](const RecordedCall& call) { return call.command_line.starts_with(prefix); }).size();
    }

 private:
    struct Rule {
        Predicate matches;
        CannedReply reply;
    };

    template <std::predicate<const RecordedCall&> Filter>
    [[nodiscard]] auto command_lines_where(Filter filter) const -> std::vector<std::string> {
        const std::scoped_lock lock(m_mutex);
        return m_calls
            | std::ranges::views::filter(std::move(filter))
            | std::ranges::views::transform(&RecordedCall::command_line)
            | std::ranges::to<std::vector<std::string>>();
    }

    auto handle(const InterceptedCommand& cmd) -> std::optional<ProcessResult> {
        std::optional<CannedReply> reply;
        {
            const std::scoped_lock lock(m_mutex);
            const auto rule = std::ranges::find_if(m_rules, [&cmd](const Predicate& matches) { return matches(cmd); }, &Rule::matches);
            if (rule != std::ranges::end(m_rules)) {
                reply = rule->reply;
            }
            m_calls.push_back(RecordedCall{
                .command_line = std::string{cmd.command_line},
                .argv         = std::vector<std::string>{cmd.argv.begin(), cmd.argv.end()},
                .kind         = cmd.options.kind,
                .location     = cmd.options.location,
                .mountpoint   = std::string{cmd.options.mountpoint},
                .input        = std::string{cmd.options.input},
                .matched      = reply.has_value(),
            });
        }
        if (reply) {
            return ProcessResult{.exit_code = reply->exit_code, .output = std::move(reply->output)};
        }
        if (m_policy == Unmatched::Fail) {
            return ProcessResult{.exit_code = kUnmatchedExitCode};
        }
        // the enclosing stub takes its own lock
        return m_previous ? m_previous(cmd) : std::nullopt;
    }

    mutable std::mutex m_mutex;
    std::vector<Rule> m_rules;
    std::vector<RecordedCall> m_calls;
    Unmatched m_policy;
    TestInterceptor m_previous;
};

}  // namespace gucc::utils::testing
