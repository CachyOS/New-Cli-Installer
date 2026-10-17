#include "gucc/process.hpp"
#include "gucc/logger.hpp"
#include "gucc/string_utils.hpp"
#include "third_party/subprocess.h"

#include <unistd.h>  // for environ

#include <cstddef>  // for size_t
#include <cstdint>  // for uint32_t
#include <cstdio>   // for fwrite, fclose
#include <cstdlib>  // for system

#include <algorithm>           // for ranges::find
#include <array>               // for array
#include <atomic>              // for atomic_bool
#include <condition_variable>  // for condition_variable
#include <iterator>            // for unreachable_sentinel
#include <memory>              // for shared_ptr, make_shared
#include <mutex>               // for mutex, scoped_lock, unique_lock
#include <ranges>              // for ranges::*
#include <string>              // for string
#include <string_view>         // for string_view
#include <thread>              // for jthread
#include <utility>             // for move
#include <vector>              // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

using gucc::utils::ProcessLocation;

namespace {

constexpr auto strip_cr(std::string_view line) noexcept -> std::string_view {
    if (line.ends_with('\r')) {
        line.remove_suffix(1);
    }
    return line;
}

constexpr auto as_string_views(char** raw) noexcept {
    // NOTE: that must be UB, but it works as of now
    return std::ranges::subrange(raw, std::unreachable_sentinel)
        | std::ranges::views::take_while([](const char* entry) { return entry != nullptr; })
        | std::ranges::views::transform([](const char* entry) { return std::string_view{entry}; });
}

// NOTE: calamares doing it? keep for now
constexpr auto is_host_tmp_var(std::string_view entry) noexcept -> bool {
    return entry.starts_with("TEMP="sv) || entry.starts_with("TEMPDIR="sv)
        || entry.starts_with("TMP="sv) || entry.starts_with("TMPDIR="sv);
}

constexpr auto build_environment(ProcessLocation proc_location) -> std::vector<std::string> {
    bool for_target = proc_location == ProcessLocation::Target;

    auto filtered_env = as_string_views(environ)
        | std::ranges::views::filter([for_target](std::string_view entry) {
              return !entry.starts_with("LC_ALL="sv) && !(for_target && is_host_tmp_var(entry));
          })
        | std::ranges::to<std::vector<std::string>>();

    // lets force to show programs output in english
    filtered_env.emplace_back("LC_ALL=C"s);
    return filtered_env;
}

auto env_ptrs_from(const std::vector<std::string>& storage) -> std::vector<const char*> {
    auto ptrs = storage
        | std::ranges::views::transform(&std::string::c_str)
        | std::ranges::to<std::vector<const char*>>();
    ptrs.emplace_back(nullptr);
    return ptrs;
}

auto display_command(const std::vector<std::string>& argv) -> std::string {
    const auto shell_at = std::ranges::search(argv, std::array{"/bin/sh"sv, "-c"sv});
    if (shell_at.empty() || shell_at.end() == argv.end()) {
        return gucc::utils::join(argv, ' ');
    }
    // keep the arch-chroot prefix, if any
    const auto prefix  = std::ranges::subrange(argv.begin(), shell_at.begin());
    const auto command = std::ranges::subrange(shell_at.end(), argv.end());
    return gucc::utils::join(std::ranges::views::concat(prefix, command), ' ');
}

[[gnu::pure]] auto child_environment(ProcessLocation proc_location) -> const std::vector<const char*>& {
    bool for_target = proc_location == ProcessLocation::Target;

    static const std::vector<std::string> host_storage   = build_environment(ProcessLocation::Host);
    static const std::vector<std::string> target_storage = build_environment(ProcessLocation::Target);
    static const std::vector<const char*> host_ptrs      = env_ptrs_from(host_storage);
    static const std::vector<const char*> target_ptrs    = env_ptrs_from(target_storage);
    return for_target ? target_ptrs : host_ptrs;
}
}  // namespace

namespace gucc::utils {

// TODO(vnepogodin): first to be rewritten in Rust

struct ProcessRunner::Impl {
    std::mutex observers_mutex;
    std::vector<ExecutionObserver*> observers;
    std::atomic_uint64_t next_id{1};

    std::atomic_bool dry_run{false};
    std::atomic_bool cancel_flag{false};

    std::mutex child_mutex;
    subprocess_s* active_child{nullptr};

    // in-flight call keeps its interceptor alive across a swap
    std::mutex interceptor_mutex;
    std::shared_ptr<const TestInterceptor> interceptor;
    // production use fast path where no interceptor is ever installed
    std::atomic_bool has_interceptor{false};

    /// @p shell_cmdline is non-empty only for run_shell
    auto launch(std::vector<std::string> argv, std::string_view shell_cmdline, const RunOptions& opts) noexcept -> ProcessResult;

    auto intercept(std::span<const std::string> argv, std::string_view shell_cmdline, const RunOptions& opts) noexcept
        -> std::optional<ProcessResult>;

    // snapshot so observers may detach while a command runs
    auto observers_snapshot() -> std::vector<ExecutionObserver*> {
        const std::scoped_lock lock(observers_mutex);
        return observers;
    }
};

void LineAssembler::feed(std::string_view chunk, const LineCallback& on_line) {
    m_buffer.append(chunk);
    std::size_t start{};
    for (auto nl = m_buffer.find('\n'); nl != std::string::npos; nl = m_buffer.find('\n', start)) {
        on_line(strip_cr(std::string_view{m_buffer}.substr(start, nl - start)));
        start = nl + 1;
    }
    m_buffer.erase(0, start);
}

void LineAssembler::flush(const LineCallback& on_line) {
    if (!m_buffer.empty()) {
        on_line(strip_cr(m_buffer));
        m_buffer.clear();
    }
}

auto ProcessRunner::Impl::intercept(std::span<const std::string> argv, std::string_view shell_cmdline, const RunOptions& opts) noexcept
    -> std::optional<ProcessResult> {
    if (!has_interceptor.load(std::memory_order_relaxed)) {
        return std::nullopt;
    }
    std::shared_ptr<const TestInterceptor> active;
    {
        const std::scoped_lock lock(interceptor_mutex);
        active = interceptor;
    }
    if (!active) {
        return std::nullopt;
    }
    const auto joined = shell_cmdline.empty()
        ? utils::join(argv, ' ')
        : std::string{};

    // ugly-ish
    return (*active)(InterceptedCommand{
        .argv         = argv,
        .command_line = shell_cmdline.empty() ? std::string_view{joined} : shell_cmdline,
        .options      = opts,
    });
}

auto ProcessRunner::Impl::launch(std::vector<std::string> argv, std::string_view shell_cmdline, const RunOptions& opts) noexcept -> ProcessResult {
    if (argv.empty()) {
        spdlog::error("[exec] refusing to run an empty command");
        return ProcessResult{.exit_code = -1};
    }

    // interceptor sees the caller argv, not the chroot wrapper
    const auto caller_argc = argv.size();
    if (opts.location == ProcessLocation::Target) {
        argv.insert(argv.begin(), {"/usr/bin/arch-chroot"s, std::string{opts.mountpoint}});
    }

    const auto active_observers = observers_snapshot();
    const CommandInfo info{
        .id      = next_id.fetch_add(1),
        .display = logger::redact(display_command(argv)),
        .options = opts,
        .skipped = dry_run.load() && opts.kind == ProcessKind::Mutate,
    };
    const auto finish = [&](const ProcessResult& result) -> ProcessResult {
        for (auto* observer : active_observers) {
            observer->on_finish(info, result);
        }
        return result;
    };

    if (cancel_flag.load()) {
        return ProcessResult{.cancelled = true, .exit_code = -1};
    }

    for (auto* observer : active_observers) {
        observer->on_start(info);
    }
    if (info.skipped) {
        return finish(ProcessResult{.exit_code = 0});
    }

    LineAssembler assembler;
    const auto emit = [&](std::string_view line) {
        if (opts.quiet) {
            return;
        }
        // redact passwords etc
        std::string owned;
        if (logger::has_secrets()) {
            owned = logger::redact(line);
            line  = owned;
        }
        for (auto* observer : active_observers) {
            observer->on_line(info, line);
        }
    };

    // shared output path
    ProcessResult result{};
    const auto consume = [&](std::string_view chunk) {
        result.output.append(chunk);
        assembler.feed(chunk, emit);
    };
    const auto conclude = [&]() -> ProcessResult {
        assembler.flush(emit);
        if (result.output.ends_with('\n')) {
            result.output.pop_back();
        }
        return finish(std::move(result));
    };

    if (auto intercepted = intercept(std::span{argv}.last(caller_argc), shell_cmdline, opts)) {
        const auto output = std::exchange(intercepted->output, {});
        result            = std::move(*intercepted);
        consume(output);
        return conclude();
    }

    auto argv_ptrs = env_ptrs_from(argv);

    const auto& base_env = child_environment(opts.location);
    std::vector<const char*> extended_env;
    if (!opts.env.empty()) {
        extended_env.assign(base_env.begin(), std::prev(base_env.end()));
        std::ranges::copy(opts.env | std::ranges::views::transform(&std::string::c_str), std::back_inserter(extended_env));
        extended_env.emplace_back(nullptr);
    }
    const auto& env_ptrs = opts.env.empty() ? base_env : extended_env;

    subprocess_s process{};
    const int options = subprocess_option_enable_async | subprocess_option_combined_stdout_stderr;
    if (subprocess_create_ex(argv_ptrs.data(), options, env_ptrs.data(), &process) != 0) {
        spdlog::error("[exec] failed to spawn: {}", info.display);
        return finish(ProcessResult{.exit_code = -1});
    }

    // written before any output is read, so only small payloads
    if (auto* child_stdin = subprocess_stdin(&process); child_stdin != nullptr) {
        if (!opts.input.empty() && std::fwrite(opts.input.data(), 1, opts.input.size(), child_stdin) != opts.input.size()) {
            spdlog::error("[exec] failed to write stdin: {}", info.display);
        }
        std::fclose(child_stdin);
        process.stdin_file = nullptr;
    }

    {
        const std::scoped_lock lock(child_mutex);
        active_child = &process;
    }

    // deadline propagation
    std::mutex done_mutex;
    std::condition_variable done_cv;
    bool done{};
    std::atomic_bool timed_out{false};
    std::jthread watchdog;
    if (opts.timeout.count() > 0) {
        watchdog = std::jthread([&] {
            {
                std::unique_lock<std::mutex> lock(done_mutex);
                if (done_cv.wait_for(lock, opts.timeout, [&] { return done; })) {
                    return;
                }
            }
            timed_out.store(true);
            const std::scoped_lock child_lock(child_mutex);
            if (active_child != nullptr) {
                subprocess_terminate(active_child);
            }
        });
    }

    std::array<char, 8192> buf{};
    std::uint32_t bytes_read{};
    do {
        bytes_read = subprocess_read_stdout(&process, buf.data(), static_cast<std::uint32_t>(buf.size()));
        if (bytes_read > 0) {
            consume(std::string_view{buf.data(), bytes_read});
        }
    } while (bytes_read != 0);

    int ret{-1};
    if (subprocess_join(&process, &ret) != 0) {
        spdlog::error("[exec] failed to join: {}", info.display);
        ret = -1;
    }

    // wake timeout watcher
    {
        const std::scoped_lock lock(done_mutex);
        done = true;
    }
    done_cv.notify_all();
    {
        const std::scoped_lock lock(child_mutex);
        active_child = nullptr;
    }
    subprocess_destroy(&process);

    result.timed_out = timed_out.load();
    result.cancelled = cancel_flag.load();
    result.exit_code = ret;
    return conclude();
}

ProcessRunner::ProcessRunner() noexcept : m_impl(std::make_unique<Impl>()) { }
ProcessRunner::~ProcessRunner() = default;

auto ProcessRunner::add_observer(ExecutionObserver& observer) noexcept -> ObserverHandle {
    const std::scoped_lock lock(m_impl->observers_mutex);
    m_impl->observers.push_back(&observer);
    return ObserverHandle{[impl = m_impl.get(), target = &observer]() noexcept {
        const std::scoped_lock lock(impl->observers_mutex);
        std::erase(impl->observers, target);
    }};
}

void ProcessRunner::set_dry_run(bool enabled) noexcept {
    m_impl->dry_run.store(enabled);
}

auto ProcessRunner::dry_run() const noexcept -> bool {
    return m_impl->dry_run.load();
}

void ProcessRunner::cancel() noexcept {
    m_impl->cancel_flag.store(true);
    const std::scoped_lock lock(m_impl->child_mutex);
    if (m_impl->active_child != nullptr) {
        subprocess_terminate(m_impl->active_child);
    }
}

auto ProcessRunner::cancelled() const noexcept -> bool {
    return m_impl->cancel_flag.load();
}

void ProcessRunner::reset_cancel() noexcept {
    m_impl->cancel_flag.store(false);
}

auto ProcessRunner::run(std::span<const std::string> argv, const RunOptions& opts) noexcept -> ProcessResult {
    return m_impl->launch(std::vector<std::string>{argv.begin(), argv.end()}, {}, opts);
}

auto ProcessRunner::run(std::initializer_list<std::string> argv, const RunOptions& opts) noexcept -> ProcessResult {
    return m_impl->launch(std::vector<std::string>{argv}, {}, opts);
}

auto ProcessRunner::run_shell(std::string_view cmdline, const RunOptions& opts) noexcept -> ProcessResult {
    return m_impl->launch({"/bin/sh"s, "-c"s, std::string{cmdline}}, cmdline, opts);
}

auto ProcessRunner::set_test_interceptor(TestInterceptor interceptor) noexcept -> TestInterceptor {
    auto next = interceptor ? std::make_shared<const TestInterceptor>(std::move(interceptor)) : nullptr;
    std::shared_ptr<const TestInterceptor> previous;
    {
        const std::scoped_lock lock(m_impl->interceptor_mutex);
        m_impl->has_interceptor.store(next != nullptr, std::memory_order_relaxed);
        previous = std::exchange(m_impl->interceptor, std::move(next));
    }
    return previous ? *previous : TestInterceptor{};
}

auto default_runner() noexcept -> ProcessRunner& {
    static ProcessRunner runner;
    return runner;
}

auto run_interactive(std::string_view cmdline) noexcept -> std::int32_t {
    // NOTE: subprocess.h can't hand the child our TTY, so this stays on system()
    return std::system(std::string{cmdline}.c_str());  // NOLINT(bugprone-command-processor,cert-env33-c)
}

}  // namespace gucc::utils
