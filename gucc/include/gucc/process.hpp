#pragma once

#include <cstdint>  // for uint8_t, int32_t, uint64_t

#include <chrono>            // for seconds
#include <functional>        // for function, move_only_function
#include <initializer_list>  // for initializer_list
#include <memory>            // for unique_ptr
#include <span>              // for span
#include <string>            // for string
#include <string_view>       // for string_view
#include <utility>           // for exchange, move

namespace gucc::utils {

/// Where a command runs. `Target` wraps in chroot.
enum class ProcessLocation : std::uint8_t {
    Host,
    Target,
};

/// Whether a command reads or changes runtime.
enum class ProcessKind : std::uint8_t {
    Query,
    Mutate,
};

/// Per-command options.
struct RunOptions {
    bool quiet{false};
    ProcessLocation location{ProcessLocation::Host};
    ProcessKind kind{ProcessKind::Mutate};
    std::string_view mountpoint{};
    std::chrono::seconds timeout{std::chrono::seconds{0}};
    std::string_view input{};
    std::span<const std::string> env{};
};

struct ProcessResult {
    bool timed_out{false};
    bool cancelled{false};

    std::int32_t exit_code{-1};
    /// Merged stdout+stderr.
    std::string output{};

    [[nodiscard]] auto ok() const noexcept -> bool {
        return exit_code == 0 && !timed_out && !cancelled;
    }
};

class LineAssembler final {
 public:
    using LineCallback = std::function<void(std::string_view)>;

    /// Consume a chunk.
    void feed(std::string_view chunk, const LineCallback& on_line);

    /// Emit any buffered partial line.
    void flush(const LineCallback& on_line);

 private:
    std::string m_buffer;
};

struct CommandInfo {
    /// Unique per runner.
    std::uint64_t id{};
    /// Loggable command with redacted secrets.
    std::string display{};
    RunOptions options{};
    /// Mutate command not run because of dry-run.
    bool skipped{false};
};

/// @brief Callbacks run on the thread that called ProcessRunner::run.
class ExecutionObserver {
 public:
    ExecutionObserver()          = default;
    virtual ~ExecutionObserver() = default;

    ExecutionObserver(const ExecutionObserver&)                    = delete;
    ExecutionObserver(ExecutionObserver&&)                         = delete;
    auto operator=(const ExecutionObserver&) -> ExecutionObserver& = delete;
    auto operator=(ExecutionObserver&&) -> ExecutionObserver&      = delete;

    virtual void on_start([[maybe_unused]] const CommandInfo& cmd) noexcept { }
    virtual void on_line([[maybe_unused]] const CommandInfo& cmd, [[maybe_unused]] std::string_view line) noexcept { }
    virtual void on_finish([[maybe_unused]] const CommandInfo& cmd, [[maybe_unused]] const ProcessResult& result) noexcept { }
};

/// @brief Detaches the observer on destruction. Must not outlive the runner.
class [[nodiscard]] ObserverHandle final {
 public:
    using Detach = std::move_only_function<void() noexcept>;

    ObserverHandle() noexcept = default;
    explicit ObserverHandle(Detach detach) noexcept : m_detach(std::move(detach)) { }
    ~ObserverHandle() { reset(); }

    ObserverHandle(const ObserverHandle&)                    = delete;
    auto operator=(const ObserverHandle&) -> ObserverHandle& = delete;

    ObserverHandle(ObserverHandle&& other) noexcept : m_detach(std::exchange(other.m_detach, nullptr)) { }
    auto operator=(ObserverHandle&& other) noexcept -> ObserverHandle& {
        if (this != &other) {
            reset();
            m_detach = std::exchange(other.m_detach, nullptr);
        }
        return *this;
    }

    void reset() noexcept {
        if (m_detach) {
            std::exchange(m_detach, nullptr)();
        }
    }

 private:
    Detach m_detach;
};

class ProcessRunner final {
 public:
    ProcessRunner() noexcept;
    ~ProcessRunner();

    ProcessRunner(const ProcessRunner&)                    = delete;
    ProcessRunner(ProcessRunner&&)                         = delete;
    auto operator=(const ProcessRunner&) -> ProcessRunner& = delete;
    auto operator=(ProcessRunner&&) -> ProcessRunner&      = delete;

    /// @p observer is not owned.
    auto add_observer(ExecutionObserver& observer) noexcept -> ObserverHandle;

    // NOTE: `Query` commands still execute.
    void set_dry_run(bool enabled) noexcept;
    [[nodiscard]] auto dry_run() const noexcept -> bool;

    /// SIGTERMs the running child.
    void cancel() noexcept;
    [[nodiscard]] auto cancelled() const noexcept -> bool;
    void reset_cancel() noexcept;

    /// argv[0] must be an absolute path.
    auto run(std::span<const std::string> argv, const RunOptions& opts = {}) noexcept -> ProcessResult;

    auto run(std::initializer_list<std::string> argv, const RunOptions& opts = {}) noexcept -> ProcessResult;

    auto run_shell(std::string_view cmdline, const RunOptions& opts = {}) noexcept -> ProcessResult;

 private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// The process-global runner.
[[nodiscard]] auto default_runner() noexcept -> ProcessRunner&;

/// Runs on the caller's tty, bypassing the runner.
auto run_interactive(std::string_view cmdline) noexcept -> std::int32_t;

}  // namespace gucc::utils
