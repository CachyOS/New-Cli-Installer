#pragma once

#include <cstdint>  // for uint8_t, int32_t

#include <chrono>            // for seconds
#include <functional>        // for function
#include <initializer_list>  // for initializer_list
#include <memory>            // for unique_ptr
#include <span>              // for span
#include <string>            // for string
#include <string_view>       // for string_view

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
};

/// Outcome of a single command.
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

/// Spawns a child process.
class ProcessRunner final {
 public:
    using LineSink = std::function<void(std::string_view)>;

    ProcessRunner() noexcept;
    ~ProcessRunner();

    ProcessRunner(const ProcessRunner&)                    = delete;
    ProcessRunner(ProcessRunner&&)                         = delete;
    auto operator=(const ProcessRunner&) -> ProcessRunner& = delete;
    auto operator=(ProcessRunner&&) -> ProcessRunner&      = delete;

    void set_line_sink(LineSink sink) noexcept;

    // NOTE: `Query` commands still execute.
    void set_dry_run(bool enabled) noexcept;
    [[nodiscard]] auto dry_run() const noexcept -> bool;

    /// SIGTERM the in-flight child
    void cancel() noexcept;
    [[nodiscard]] auto cancelled() const noexcept -> bool;
    void reset_cancel() noexcept;

    // NOTE: exe path must be explicit
    auto run(std::span<const std::string> argv, const RunOptions& opts = {}) noexcept -> ProcessResult;

    auto run(std::initializer_list<std::string> argv, const RunOptions& opts = {}) noexcept -> ProcessResult;

    auto run_shell(std::string_view cmdline, const RunOptions& opts = {}) noexcept -> ProcessResult;

 private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace gucc::utils
