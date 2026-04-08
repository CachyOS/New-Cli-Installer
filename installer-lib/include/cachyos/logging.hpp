#ifndef CACHYOS_INSTALLER_LOGGING_HPP
#define CACHYOS_INSTALLER_LOGGING_HPP

#include <functional>   // for function
#include <memory>       // for shared_ptr
#include <string_view>  // for string_view

namespace spdlog {
class logger;
}  // namespace spdlog

namespace cachyos::installer::logging {

/// The single decorated pattern shared by every sink (file, stdout, callback)
/// so all four frontends emit byte-identical log lines (modulo timestamps).
/// The `%^/%$` colour range is honoured only by the colourised stdout sink and
/// is a no-op for the file and callback sinks.
inline constexpr std::string_view kPattern = "[%r][%^---%L---%$] %v";

/// Build the shared async logger with a single file sink at @p log_file and
/// install it as both the spdlog default and the gucc library logger. This is
/// the one place the logger is bootstrapped; every frontend calls it once at
/// startup. Returns the created logger.
auto init(std::string_view log_file = "/tmp/cachyos-install.log") -> std::shared_ptr<spdlog::logger>;

/// Attach a colourised stdout sink (decorated with kPattern). Used by the CLI
/// headless frontend and by the simple-TUI frontend once its wizard has closed,
/// so installer output streams to the terminal like a shell script. Never call
/// this in advanced-TUI mode, where ftxui owns the terminal.
void attach_stdout_sink() noexcept;

/// Attach a callback sink that hands each fully-formatted (decorated) record to
/// @p on_line, with no trailing newline. Used by the GUI (forward to QML) so the
/// live view shows exactly what the file/stdout sinks contain.
void attach_callback_sink(std::function<void(std::string_view)> on_line) noexcept;

}  // namespace cachyos::installer::logging

#endif  // CACHYOS_INSTALLER_LOGGING_HPP
