#ifndef TUI_HPP
#define TUI_HPP

#include <ftxui/component/component.hpp>

namespace tui {
ftxui::Element centered_widget(ftxui::Component& container, const std::string_view& title, const ftxui::Element& widget);
void init() noexcept;
}  // namespace tui

#endif  // TUI_HPP
