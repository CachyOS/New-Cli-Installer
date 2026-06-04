#ifndef FOLLOW_PROCESS_LOG_HPP
#define FOLLOW_PROCESS_LOG_HPP

#include <functional>  // for function
#include <stop_token>  // for stop_token
#include <string>      // for string
#include <string_view> // for string_view
#include <vector>      // for vector

#include <ftxui/dom/elements.hpp>  // for size, GREATER_THAN

// forward-declare
namespace gucc::utils {
class SubProcess;
}

namespace tui::detail {

// Run a single command and display its output in a TUI widget.
auto follow_process_log_widget(const std::vector<std::string>& vec, ftxui::Decorator box_size = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5)) noexcept -> bool;

// functor for task spawn handle
// anything run within that process task will use same log/display context
using ProcessTask = std::function<bool(gucc::utils::SubProcess& child)>;

// Run an arbitrary multi-step task that internally uses exec_follow,
// while displaying the accumulated log in a TUI widget.
// Returns the bool produced by the task.
auto follow_process_log_task(ProcessTask task, ftxui::Decorator box_size = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5)) noexcept -> bool;

// Non-TUI variant: runs the task while printing output to stdout.
auto follow_process_log_task_stdout(ProcessTask task) noexcept -> bool;

/// Callback matching the shape used by `cachyos::installer::steps::*`. The
/// runner is handed a per-line log sink and a stop token; it returns true
/// on success. Frontends drive the runner inside a follow widget so a
/// single steps::* call appears live to the user without exposing the
/// internal SubProcess.
using StepLogCallback = std::function<void(std::string_view)>;
using StepRunner      = std::function<bool(StepLogCallback log_cb, std::stop_token stop_token)>;

/// Drive a `steps::*`-shaped runner inside a ftxui screen, showing live
/// log output. The "Back" button requests cancellation via the stop token
/// while the runner is still in-flight; once the runner returns, the same
/// button closes the screen.
auto follow_step_widget(StepRunner runner, ftxui::Decorator box_size = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5)) noexcept -> bool;

/// Drive a `steps::*`-shaped runner, printing live log output to stdout.
/// Used by headless and simple-mode flows that don't have ftxui in front.
auto follow_step_stdout(StepRunner runner) noexcept -> bool;

}  // namespace tui::detail

#endif  // FOLLOW_PROCESS_LOG_HPP
