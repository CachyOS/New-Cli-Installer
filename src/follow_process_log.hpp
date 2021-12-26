#ifndef FOLLOW_PROCESS_LOG_HPP
#define FOLLOW_PROCESS_LOG_HPP

#include <string_view>  // for string_view

#include <ftxui/dom/elements.hpp>  // for size, GREATER_THAN

namespace tui {
namespace detail {
    void follow_process_log_widget(const std::vector<std::string>& vec, ftxui::Decorator boxsize = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5)) noexcept;
}  // namespace detail
}  // namespace tui

#endif  // FOLLOW_PROCESS_LOG_HPP
