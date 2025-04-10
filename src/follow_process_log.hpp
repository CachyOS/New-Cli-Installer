#ifndef FOLLOW_PROCESS_LOG_HPP
#define FOLLOW_PROCESS_LOG_HPP

#include <string>  // for string
#include <vector>  // for vector

#include <ftxui/dom/elements.hpp>  // for size, GREATER_THAN

namespace tui::detail {
auto follow_process_log_widget(const std::vector<std::string>& vec, ftxui::Decorator box_size = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5)) noexcept -> bool;
}  // namespace tui::detail

#endif  // FOLLOW_PROCESS_LOG_HPP
