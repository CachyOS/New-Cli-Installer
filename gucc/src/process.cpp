#include "gucc/process.hpp"
#include "gucc/logger.hpp"
#include "gucc/string_utils.hpp"
#include "third_party/subprocess.h"

#include <unistd.h>  // for environ

#include <cstddef>  // for size_t
#include <cstdint>  // for uint32_t

#include <array>               // for array
#include <atomic>              // for atomic_bool
#include <condition_variable>  // for condition_variable
#include <iterator>            // for unreachable_sentinel
#include <mutex>               // for mutex, lock_guard, unique_lock
#include <ranges>              // for ranges::*
#include <string>              // for string
#include <string_view>         // for string_view
#include <thread>              // for jthread
#include <utility>             // for move
#include <vector>              // for vector

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
    std::mutex sink_mutex;
    LineSink line_sink;

    std::atomic_bool dry_run{false};
    std::atomic_bool cancel_flag{false};

    std::mutex child_mutex;
    subprocess_s* active_child{nullptr};

    auto launch(std::vector<std::string> argv, const RunOptions& opts) noexcept -> ProcessResult;
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

auto ProcessRunner::Impl::launch(std::vector<std::string> argv, const RunOptions& opts) noexcept -> ProcessResult {
    if (argv.empty()) {
        spdlog::error("[exec] refusing to run an empty command");
        return ProcessResult{.exit_code = -1};
    }

    if (opts.location == ProcessLocation::Target) {
        argv.insert(argv.begin(), {"/usr/bin/arch-chroot"s, std::string{opts.mountpoint}});
    }

    const auto joined = logger::redact(utils::join(argv, ' '));
    spdlog::debug("[exec] cmd := {}", joined);

    if (cancel_flag.load()) {
        return ProcessResult{.exit_code = -1, .cancelled = true};
    }

    if (dry_run.load() && opts.kind == ProcessKind::Mutate) {
        spdlog::info("[dry-run] would run: {}", joined);
        return ProcessResult{.exit_code = 0};
    }

    auto argv_ptrs = env_ptrs_from(argv);

    const auto& env_ptrs = child_environment(opts.location);

    subprocess_s process{};
    const int options = subprocess_option_enable_async | subprocess_option_combined_stdout_stderr;
    if (subprocess_create_ex(argv_ptrs.data(), options, env_ptrs.data(), &process) != 0) {
        spdlog::error("[exec] failed to spawn: {}", joined);
        return ProcessResult{.exit_code = -1};
    }

    {
        const std::lock_guard<std::mutex> lock(child_mutex);
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
            const std::lock_guard<std::mutex> child_lock(child_mutex);
            if (active_child != nullptr) {
                subprocess_terminate(active_child);
            }
        });
    }

    LineSink sink_copy;
    {
        const std::lock_guard<std::mutex> lock(sink_mutex);
        sink_copy = line_sink;
    }

    ProcessResult result{};
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
        spdlog::info("{}", line);
        if (sink_copy) {
            sink_copy(line);
        }
    };

    std::array<char, 8192> buf{};
    std::uint32_t bytes_read{};
    do {
        bytes_read = subprocess_read_stdout(&process, buf.data(), static_cast<std::uint32_t>(buf.size()));
        if (bytes_read > 0) {
            const std::string_view chunk{buf.data(), bytes_read};
            result.output.append(chunk);
            assembler.feed(chunk, emit);
        }
    } while (bytes_read != 0);
    assembler.flush(emit);

    int ret{-1};
    if (subprocess_join(&process, &ret) != 0) {
        spdlog::error("[exec] failed to join: {}", joined);
        ret = -1;
    }

    // wake timeout watcher
    {
        const std::lock_guard<std::mutex> lock(done_mutex);
        done = true;
    }
    done_cv.notify_all();
    {
        const std::lock_guard<std::mutex> lock(child_mutex);
        active_child = nullptr;
    }
    subprocess_destroy(&process);

    if (result.output.ends_with('\n')) {
        result.output.pop_back();
    }

    result.timed_out = timed_out.load();
    result.cancelled = cancel_flag.load();
    result.exit_code = ret;
    return result;
}

ProcessRunner::ProcessRunner() noexcept : m_impl(std::make_unique<Impl>()) { }
ProcessRunner::~ProcessRunner() = default;

void ProcessRunner::set_line_sink(LineSink sink) noexcept {
    const std::lock_guard<std::mutex> lock(m_impl->sink_mutex);
    m_impl->line_sink = std::move(sink);
}

void ProcessRunner::set_dry_run(bool enabled) noexcept {
    m_impl->dry_run.store(enabled);
}

auto ProcessRunner::dry_run() const noexcept -> bool {
    return m_impl->dry_run.load();
}

void ProcessRunner::cancel() noexcept {
    m_impl->cancel_flag.store(true);
    const std::lock_guard<std::mutex> lock(m_impl->child_mutex);
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
    return m_impl->launch(std::vector<std::string>{argv.begin(), argv.end()}, opts);
}

auto ProcessRunner::run(std::initializer_list<std::string> argv, const RunOptions& opts) noexcept -> ProcessResult {
    return m_impl->launch(std::vector<std::string>{argv}, opts);
}

auto ProcessRunner::run_shell(std::string_view cmdline, const RunOptions& opts) noexcept -> ProcessResult {
    return m_impl->launch({"/bin/sh"s, "-c"s, std::string{cmdline}}, opts);
}

}  // namespace gucc::utils
