#ifndef FOLLOW_PROCESS_LOG_HPP
#define FOLLOW_PROCESS_LOG_HPP

#include <string_view>  // for string_view
#include <vector>       // for vector

#include <ftxui/dom/elements.hpp>  // for size, GREATER_THAN

namespace tui::detail {
void follow_process_log_widget(const std::vector<std::string>& vec, ftxui::Decorator box_size = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5)) noexcept;
}  // namespace tui::detail

#endif  // FOLLOW_PROCESS_LOG_HPP
