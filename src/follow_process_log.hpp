#pragma once

#include <functional>  // for function
#include <string>      // for string
#include <vector>      // for vector

#include <ftxui/dom/elements.hpp>  // for size, GREATER_THAN

namespace tui::detail {

// functor for task spawn handle
using ProcessTask = std::function<bool()>;

/// Newest line first. "Back" cancels while @p task runs and closes the screen afterwards.
auto follow_process_log_task(ProcessTask task, ftxui::Decorator box_size = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5)) noexcept -> bool;

auto follow_process_log_task_stdout(ProcessTask task) noexcept -> bool;

auto follow_task(ProcessTask task) noexcept -> bool;

auto follow_process_log_widget(const std::vector<std::string>& vec, ftxui::Decorator box_size = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5)) noexcept -> bool;

}  // namespace tui::detail
