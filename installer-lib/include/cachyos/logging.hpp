#pragma once

#include <functional>   // for function
#include <memory>       // for shared_ptr
#include <string_view>  // for string_view

namespace spdlog {
class logger;
}  // namespace spdlog

namespace cachyos::installer::logging {

/// The decorated pattern shared by every sink.
inline constexpr std::string_view kPattern = "[%r][%^---%L---%$] %v";

/// Initialize default spdlog async logger sinks.
auto init(std::string_view log_file = "/tmp/cachyos-install.log") -> std::shared_ptr<spdlog::logger>;

/// Attach stdout sink so installer output streams to the terminal.
void attach_stdout_sink() noexcept;

/// Remove the stdout sink. Required for proper TUI.
void detach_stdout_sink() noexcept;

/// Attach a sink that hands each formatted record (no trailing newline) to
/// @p on_line.
void attach_callback_sink(std::function<void(std::string_view)> on_line) noexcept;

/// Remove the callback sink.
void detach_callback_sink() noexcept;

}  // namespace cachyos::installer::logging
