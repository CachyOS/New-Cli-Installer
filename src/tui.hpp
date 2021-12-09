#ifndef TUI_HPP
#define TUI_HPP

/* clang-format off */
#include <string_view>                           // for string_view
#include <ftxui/component/component_base.hpp>  // for Component
#include <ftxui/dom/elements.hpp>                // for Element
/* clang-format on */

namespace tui {
ftxui::Element centered_widget(ftxui::Component& container, const std::string_view& title, const ftxui::Element& widget);
void create_partitions() noexcept;
void init() noexcept;
}  // namespace tui

#endif  // TUI_HPP
