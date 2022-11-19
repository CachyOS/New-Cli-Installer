#ifndef WIDGETS_HPP
#define WIDGETS_HPP

#include <array>        // for array
#include <cstdint>      // for int32_t
#include <functional>   // for function
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <ftxui/component/component_base.hpp>      // for Components
#include <ftxui/component/screen_interactive.hpp>  // for Component
#include <ftxui/dom/elements.hpp>                  // for size, GREATER_THAN

/* clang-format off */
namespace ftxui { struct ButtonOption; }
/* clang-format on */

namespace tui {
namespace detail {
    struct WidgetBoxSize {
        ftxui::Decorator content_size = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 10) | size(ftxui::WIDTH, ftxui::GREATER_THAN, 40);
        ftxui::Decorator text_size    = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5);
    };
    struct WidgetBoxRes {
        const std::string_view text{};
        const std::string_view title{"New CLI Installer"};
    };

    auto centered_widget(ftxui::Component& container, const std::string_view& title, const ftxui::Element& widget) noexcept -> ftxui::Element;
    auto controls_widget(const std::array<std::string_view, 2>&& titles, const std::array<std::function<void()>, 2>&& callbacks, ftxui::ButtonOption* button_option) noexcept -> ftxui::Component;
    auto centered_interative_multi(const std::string_view& title, ftxui::Component& widgets) noexcept -> ftxui::Element;
    auto multiline_text(const std::vector<std::string>& lines) noexcept -> ftxui::Element;
    auto from_vector_checklist(const std::vector<std::string>& opts, bool* opts_state) noexcept -> ftxui::Components;
    auto from_checklist_string(const std::vector<std::string>& opts, bool* opts_state) noexcept -> std::string;
    auto from_checklist_vector(const std::vector<std::string>& opts, bool* opts_state) noexcept -> std::vector<std::string>;
    void msgbox_widget(const std::string_view& content, ftxui::Decorator boxsize = ftxui::hcenter | size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5)) noexcept;
    bool inputbox_widget(std::string& value, const std::string_view& content, ftxui::Decorator boxsize = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5), bool password = false) noexcept;
    void infobox_widget(const std::string_view& content, ftxui::Decorator boxsize = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5)) noexcept;
    bool yesno_widget(const std::string_view& content, ftxui::Decorator boxsize = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5)) noexcept;
    bool yesno_widget(ftxui::Component& container, ftxui::Decorator boxsize = size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5)) noexcept;
    void menu_widget(const std::vector<std::string>& entries, const std::function<void()>&& ok_callback, std::int32_t* selected, ftxui::ScreenInteractive* screen, const std::string_view& text = "", const WidgetBoxSize widget_sizes = {}) noexcept;
    void radiolist_widget(const std::vector<std::string>& entries, const std::function<void()>&& ok_callback, std::int32_t* selected, ftxui::ScreenInteractive* screen, const WidgetBoxRes& widget_res = {}, const WidgetBoxSize widget_sizes = {}) noexcept;
    void checklist_widget(const std::vector<std::string>& opts, const std::function<void()>&& ok_callback, bool* opts_state, ftxui::ScreenInteractive* screen, const std::string_view& text = "", const std::string_view& title = "New CLI Installer", const WidgetBoxSize widget_sizes = {}) noexcept;
}  // namespace detail
}  // namespace tui

#endif  // WIDGETS_HPP
