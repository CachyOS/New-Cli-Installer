#ifndef FOLLOW_PROCESS_LOG_HPP
#define FOLLOW_PROCESS_LOG_HPP

#include <functional>  // for function
#include <string>      // for string
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

}  // namespace tui::detail

#endif  // FOLLOW_PROCESS_LOG_HPP
