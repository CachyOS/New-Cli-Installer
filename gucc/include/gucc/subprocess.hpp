#ifndef SUBPROCESS_HPP
#define SUBPROCESS_HPP

#include <atomic>       // for atomic_bool
#include <memory>       // for unique_ptr
#include <mutex>        // for mutex
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::utils {

// Wrapper around thirdparty subprocess handle
class SubProcess final {
 public:
    SubProcess();
    ~SubProcess();

    // explicitly deleted (move-only)
    SubProcess(const SubProcess&)     = delete;
    auto operator=(const SubProcess&) = delete;

    SubProcess(SubProcess&& other) noexcept;
    auto operator=(SubProcess&& other) noexcept -> SubProcess&;

    /// @return true when the process has been spawned and has a valid child pid.
    [[nodiscard]] auto has_child() const noexcept -> bool;

    /// @brief Send SIGTERM/TerminateProcess.
    /// @return true on success.
    auto terminate() noexcept -> bool;

    /// @brief Release internal resources.
    /// @return true on success.
    auto destroy() noexcept -> bool;

    /// @brief Get a snapshot of the accumulated log.
    [[nodiscard]] auto get_log() const noexcept -> std::string;

    /// @brief Append text to the accumulated log.
    void append_log(std::string_view text) noexcept;

    /// @brief Whether the subprocess is still running.
    std::atomic_bool running{true};

 private:
    friend auto exec_follow(const std::vector<std::string>& vec,
        SubProcess& child, bool async) noexcept -> bool;
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    mutable std::mutex m_log_mutex;
    std::string m_process_log;
};

/// @brief Execute command args via subprocess with async combined stdout+stderr.
/// @param vec The arguments to launch.
/// @param child Subprocess handle.
/// @param async Execution mode.
/// @return True on success.
auto exec_follow(const std::vector<std::string>& vec, SubProcess& child, bool async = true) noexcept -> bool;

}  // namespace gucc::utils

#endif  // SUBPROCESS_HPP
