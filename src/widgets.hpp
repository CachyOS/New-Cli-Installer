#ifndef WIDGETS_HPP
#define WIDGETS_HPP

/* clang-format off */
#include <string_view>                             // for string_view
#include <ftxui/component/component_base.hpp>      // for Component
#include <ftxui/component/component_options.hpp>   // for ButtonOption
#include <ftxui/dom/elements.hpp>                  // for Element, operator|, size
#include <ftxui/component/screen_interactive.hpp>
/* clang-format on */

namespace tui {
namespace detail {
    struct ContentBoxSize {
        int height;
        int width;
        ftxui::Decorator text_size = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5);
    };

    auto centered_widget(ftxui::Component& container, const std::string_view& title, const ftxui::Element& widget) noexcept -> ftxui::Element;
    auto controls_widget(const std::array<std::string_view, 2>&& titles, const std::array<std::function<void()>, 2>&& callbacks, ftxui::ButtonOption* button_option) noexcept -> ftxui::Component;
    auto centered_interative_multi(const std::string_view& title, ftxui::Component& widgets) noexcept -> ftxui::Element;
    auto multiline_text(const std::vector<std::string>& lines) noexcept -> ftxui::Element;
    auto from_vector_checklist(const std::vector<std::string>& opts, bool* opts_state) noexcept -> ftxui::Components;
    auto from_checklist_string(const std::vector<std::string>& opts, bool* opts_state) noexcept -> std::string;
    auto from_checklist_vector(const std::vector<std::string>& opts, bool* opts_state) noexcept -> std::vector<std::string>;
    void msgbox_widget(const std::string_view& content, ftxui::Decorator boxsize = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5)) noexcept;
    bool inputbox_widget(std::string& value, const std::string_view& content, ftxui::Decorator boxsize = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5), bool password = false) noexcept;
    void infobox_widget(const std::string_view& content, ftxui::Decorator boxsize = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5)) noexcept;
    bool yesno_widget(const std::string_view& content, ftxui::Decorator boxsize = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5)) noexcept;
    bool yesno_widget(ftxui::Component& container, ftxui::Decorator boxsize = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5)) noexcept;
    void menu_widget(const std::vector<std::string>& entries, const std::function<void()>&& ok_callback, std::int32_t* selected, ftxui::ScreenInteractive* screen, const std::string_view& text = "", const ContentBoxSize content_size = {10, 40}) noexcept;
}  // namespace detail
}  // namespace tui

#endif  // WIDGETS_HPP
