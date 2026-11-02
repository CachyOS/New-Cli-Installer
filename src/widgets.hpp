#pragma once

#include <cstddef>  // for size_t
#include <cstdint>  // for int32_t

#include <chrono>       // for seconds
#include <functional>   // for function, function_ref
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <ftxui/component/component_base.hpp>      // for Component
#include <ftxui/component/screen_interactive.hpp>  // for ScreenInteractive
#include <ftxui/dom/elements.hpp>                  // for Element

namespace tui::detail {

inline constexpr std::string_view kDefaultTitle{"New CLI Installer"};

struct MenuEntry {
    std::string label;
    std::function<bool()> action;
};
inline constexpr auto kLeaveMenu = [] { return false; };

auto centered_widget(ftxui::Component& container, std::string_view title, const ftxui::Element& widget) noexcept -> ftxui::Element;
auto multiline_text(std::string_view content) noexcept -> ftxui::Element;
auto multiline_text(const std::vector<std::string>& lines) noexcept -> ftxui::Element;

void msgbox_widget(std::string_view content) noexcept;
bool yesno_widget(std::string_view content) noexcept;
bool inputbox_widget(std::string& value, std::string_view content, bool password = false) noexcept;
auto password_prompt(std::string_view content, std::string_view confirm_content) noexcept -> std::optional<std::string>;
void infobox_widget(std::string_view content) noexcept;
void infobox_for(std::string_view content, std::chrono::seconds delay) noexcept;
auto prompt_validated(std::string_view content, std::string_view error_content, std::function_ref<bool(std::string_view)> is_valid, std::string initial = {}) noexcept -> std::optional<std::string>;

auto select_from_menu(const std::vector<std::string>& entries, std::string_view text = {}, std::size_t initial = 0) noexcept -> std::optional<std::size_t>;
auto select_value(const std::vector<std::string>& entries, std::string_view text = {}, std::size_t initial = 0) noexcept -> std::optional<std::string>;
auto select_radio(const std::vector<std::string>& entries, std::string_view text = {}, std::string_view title = kDefaultTitle) noexcept -> std::optional<std::string>;
auto select_many(const std::vector<std::string>& entries, std::string_view text = {}, std::string_view title = kDefaultTitle,
    std::function<bool(const std::vector<std::string>&)> accept = {}) noexcept -> std::optional<std::vector<std::string>>;
auto run_in_terminal(const std::vector<std::string>& argv, std::string_view input = {}) noexcept -> std::int32_t;

void menu_loop(std::string_view text, const std::vector<MenuEntry>& entries) noexcept;

// Wizard step builders
auto wizard_menu_step(std::string_view label, const std::vector<std::string>& entries,
    std::int32_t* selected, std::function<void()> on_enter) noexcept -> ftxui::Component;
auto wizard_input_step(std::string_view label, std::string* value,
    std::string_view placeholder, bool password,
    std::function<void()> on_enter) noexcept -> ftxui::Component;

}  // namespace tui::detail
