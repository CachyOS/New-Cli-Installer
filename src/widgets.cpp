#include "widgets.hpp"

// import gucc
#include "gucc/process.hpp"
#include "gucc/string_utils.hpp"

#include <cstddef>  // for size_t
#include <cstdint>  // for int32_t
#include <cstdio>   // for fflush, stdout

#include <algorithm>   // for clamp, max, min, fold_left
#include <functional>  // for plus
#include <memory>      // for unique_ptr, make_unique
#include <optional>    // for optional
#include <ranges>      // for ranges::*
#include <string>      // for string
#include <thread>      // for sleep_for
#include <utility>     // for move, pair

#include <fmt/compile.h>
#include <fmt/format.h>

#include <ftxui/component/component.hpp>           // for Renderer, Vertical, Menu
#include <ftxui/component/component_base.hpp>      // for ComponentBase
#include <ftxui/component/component_options.hpp>   // for ButtonOption, InputOption, MenuOption
#include <ftxui/component/event.hpp>               // for Event
#include <ftxui/component/screen_interactive.hpp>  // for ScreenInteractive
#include <ftxui/dom/elements.hpp>                  // for operator|, Element
#include <ftxui/dom/node.hpp>                      // for Render
#include <ftxui/screen/screen.hpp>                 // for Screen
#include <ftxui/screen/terminal.hpp>               // for Size

using namespace ftxui;
using namespace std::string_view_literals;

namespace {

// title, border, two separators, buttons and a margin
constexpr std::int32_t kChromeRows{8};
constexpr std::int32_t kMinWidth{40};
constexpr auto kToggleHint = "Use [Spacebar] to de/select options listed."sv;

struct Dialog {
    std::string_view title{tui::detail::kDefaultTitle};
    std::string_view text{};
    /// Interactive gets the initial focus.
    Component input{};
    std::int32_t input_rows{};
    bool scroll{true};
    std::string_view hint{};
};

auto text_width_limit() noexcept -> std::int32_t {
    // border and side margins
    return std::clamp(Terminal::Size().dimx - 8, 30, 76);
}

auto available_rows() noexcept -> std::int32_t {
    return std::max(Terminal::Size().dimy - kChromeRows, 6);
}

constexpr auto trim_newlines(std::string_view content) noexcept -> std::string_view {
    const auto first = content.find_first_not_of('\n');
    if (first == std::string_view::npos) {
        return {};
    }
    return content.substr(first, content.find_last_not_of('\n') - first + 1);
}

auto wrapped_rows(std::string_view content) noexcept -> std::int32_t {
    const auto width     = static_cast<std::size_t>(text_width_limit());
    const auto line_rows = [width](std::string_view line) { return std::max<std::size_t>(1, (line.size() + width - 1) / width); };
    return static_cast<std::int32_t>(std::ranges::fold_left(gucc::utils::split_lines(trim_newlines(content)) | std::ranges::views::transform(line_rows), std::size_t{}, std::plus{}));
}

auto split_rows(std::string_view text, std::int32_t list_rows) noexcept -> std::pair<std::int32_t, std::int32_t> {
    const auto avail     = available_rows();
    const auto text_rows = text.empty() ? 0 : wrapped_rows(text);
    if (text_rows + list_rows <= avail) {
        return {text_rows, list_rows};
    }
    const auto list = list_rows == 0 ? 0 : std::max(avail - text_rows, std::min(list_rows, avail / 2));
    return {std::max(avail - list, 1), list};
}

auto framed(std::string_view title, Element body) noexcept -> Element {
    return vbox({
        text(std::string{title}) | bold,
        filler(),
        hbox({
            filler(),
            border(std::move(body) | size(WIDTH, GREATER_THAN, kMinWidth)),
            filler(),
        }) | center,
        filler(),
    });
}

auto buttons(std::function<void()> on_ok, std::function<void()> on_cancel) noexcept -> Component {
    auto row = on_cancel
        ? Container::Horizontal({
              Button("OK", std::move(on_ok), ButtonOption::WithoutBorder()),
              Renderer([] { return filler() | size(WIDTH, GREATER_THAN, 3); }),
              Button("Cancel", std::move(on_cancel), ButtonOption::WithoutBorder()),
          })
        : Container::Horizontal({Button("OK", std::move(on_ok), ButtonOption::WithoutBorder())});
    return Renderer(row, [row] { return row->Render() | hcenter; });
}

auto wrap_lines(const std::vector<std::string>& lines, std::int32_t width) noexcept -> Element {
    // paragraph wraps to the width
    auto rows = lines
        | std::ranges::views::transform([](const std::string& line) -> Element { return line.empty() ? text("") : paragraphAlignLeft(line); })
        | std::ranges::to<Elements>();
    return vbox(std::move(rows)) | size(WIDTH, LESS_THAN, width) | yframe;
}

auto to_lines(std::string_view content) noexcept -> std::vector<std::string> {
    return gucc::utils::split_lines(trim_newlines(content)) | std::ranges::to<std::vector<std::string>>();
}

class DialogLoop final {
 public:
    auto ok(std::function<bool()> accept = {}) noexcept -> std::function<void()> {
        return [this, accept = std::move(accept)] {
            if (!accept || accept()) {
                m_confirmed = true;
                m_screen.Exit();
            }
        };
    }

    /// True when the dialog was closed by OK.
    auto run(const Dialog& dialog, std::function<void()> on_ok, bool cancellable = true) noexcept -> bool {
        // sized once
        const auto width                   = text_width_limit();
        const auto [text_rows, input_rows] = split_rows(dialog.text, dialog.input ? dialog.input_rows : 0);
        const auto title                   = std::string{dialog.title};

        Components children{};
        const auto add_separator = [&children] {
            if (!children.empty()) {
                children.push_back(Renderer([] { return separator(); }));
            }
        };
        if (!dialog.text.empty()) {
            children.push_back(Renderer([lines = to_lines(dialog.text), width, rows = text_rows] { return wrap_lines(lines, width) | size(HEIGHT, LESS_THAN, rows); }));
        }
        if (dialog.input) {
            add_separator();
            children.push_back(Renderer(dialog.input, [&dialog, width, rows = input_rows, hint = std::string{dialog.hint}] {
                auto element = dialog.scroll
                    ? dialog.input->Render() | hcenter | vscroll_indicator | yframe | size(HEIGHT, LESS_THAN, rows)
                    : dialog.input->Render() | size(WIDTH, EQUAL, width);
                if (hint.empty()) {
                    return element;
                }
                return vbox({std::move(element), text(hint) | dim | hcenter});
            }));
        }
        add_separator();

        // focus on input, or at OK btn
        auto focused = static_cast<std::int32_t>(dialog.input ? children.size() - 2 : children.size());
        children.push_back(buttons(std::move(on_ok), cancellable ? m_screen.ExitLoopClosure() : std::function<void()>{}));

        auto body = Container::Vertical(std::move(children), &focused);
        m_screen.Loop(Renderer(body, [&title, body] { return framed(title, body->Render()); }));
        return m_confirmed;
    }

 private:
    ScreenInteractive m_screen{ScreenInteractive::Fullscreen()};
    bool m_confirmed{};
};

auto confirm_on_enter(Component list, std::function<void()> on_ok, bool forward) noexcept -> Component {
    return CatchEvent(list, [list, on_ok = std::move(on_ok), forward](const Event& event) {
        if (event != Event::Return) {
            return false;
        }
        if (forward) {
            list->OnEvent(event);
        }
        on_ok();
        return true;
    });
}

auto rows_of(const std::vector<std::string>& entries) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(entries.size());
}

}  // namespace

namespace tui::detail {

Element centered_widget(Component& container, std::string_view title, const Element& widget) noexcept {
    return framed(title, vbox({widget, separator(), container->Render() | hcenter}));
}

Element multiline_text(std::string_view content) noexcept {
    return wrap_lines(to_lines(content), text_width_limit());
}

Element multiline_text(const std::vector<std::string>& lines) noexcept {
    return wrap_lines(lines, text_width_limit());
}

void msgbox_widget(std::string_view content) noexcept {
    DialogLoop dialog;
    dialog.run({.text = content}, dialog.ok(), false);
}

bool yesno_widget(std::string_view content) noexcept {
    DialogLoop dialog;
    return dialog.run({.text = content}, dialog.ok());
}

bool inputbox_widget(std::string& value, std::string_view content, bool password) noexcept {
    DialogLoop dialog;
    const auto on_ok = dialog.ok();
    InputOption option{};
    option.password  = password;
    option.multiline = false;
    option.on_enter  = on_ok;
    return dialog.run({.text = content, .input = Input(&value, "", option), .input_rows = 1, .scroll = false}, on_ok);
}

auto password_prompt(std::string_view content, std::string_view confirm_content) noexcept -> std::optional<std::string> {
    while (true) {
        std::string pass{};
        std::string confirm{};
        if (!detail::inputbox_widget(pass, content, true) || !detail::inputbox_widget(confirm, confirm_content, true)) {
            return std::nullopt;
        }
        if (gucc::utils::trim(pass) == gucc::utils::trim(confirm)) {
            return std::string{gucc::utils::trim(pass)};
        }
        detail::msgbox_widget("The passwords entered do not match.\nPlease try again."sv);
    }
}

void infobox_widget(std::string_view content) noexcept {
    auto screen = Screen::Create(Dimension::Full(), Dimension::Full());
    Render(screen, framed(kDefaultTitle, multiline_text(content) | size(HEIGHT, LESS_THAN, available_rows())));

    // should fix frame re-renders
    fmt::print(FMT_COMPILE("\x1B[H{}"), screen.ToString());
    std::fflush(stdout);
}

void infobox_for(std::string_view content, std::chrono::seconds delay) noexcept {
    detail::infobox_widget(content);
    std::this_thread::sleep_for(delay);
}

auto prompt_validated(std::string_view content, std::string_view error_content, std::function_ref<bool(std::string_view)> is_valid, std::string initial) noexcept -> std::optional<std::string> {
    std::string value{std::move(initial)};
    for (auto body = content;; body = error_content) {
        if (!detail::inputbox_widget(value, body)) {
            return std::nullopt;
        }
        value = std::string{gucc::utils::trim(value)};
        if (is_valid(value)) {
            return value;
        }
        value.clear();
    }
}

auto select_from_menu(const std::vector<std::string>& entries, std::string_view text, std::size_t initial) noexcept -> std::optional<std::size_t> {
    DialogLoop dialog;
    auto selected   = static_cast<std::int32_t>(initial);
    auto option     = MenuOption::Vertical();
    option.on_enter = dialog.ok();
    if (!dialog.run({.text = text, .input = Menu(&entries, &selected, option), .input_rows = rows_of(entries)}, option.on_enter)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(selected);
}

auto select_value(const std::vector<std::string>& entries, std::string_view text, std::size_t initial) noexcept -> std::optional<std::string> {
    return detail::select_from_menu(entries, text, initial).transform([&entries](std::size_t idx) { return entries.at(idx); });
}

auto select_radio(const std::vector<std::string>& entries, std::string_view text, std::string_view title) noexcept -> std::optional<std::string> {
    DialogLoop dialog;
    std::int32_t selected{};
    const auto on_ok = dialog.ok();
    if (!dialog.run({.title = title, .text = text, .input = confirm_on_enter(Radiobox(&entries, &selected), on_ok, true), .input_rows = rows_of(entries), .hint = kToggleHint}, on_ok)) {
        return std::nullopt;
    }
    return entries.at(static_cast<std::size_t>(selected));
}

auto select_many(const std::vector<std::string>& entries, std::string_view text, std::string_view title,
    std::function<bool(const std::vector<std::string>&)> accept) noexcept -> std::optional<std::vector<std::string>> {
    // convert it into bool array instead of vector of bools
    auto checked = std::make_unique<bool[]>(entries.size());
    auto boxes   = std::ranges::views::iota(std::size_t{}, entries.size())
        | std::ranges::views::transform([&](std::size_t idx) { return Checkbox(&entries.at(idx), &checked[idx]); })  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        | std::ranges::to<Components>();

    DialogLoop dialog;
    std::vector<std::string> picked{};
    const auto on_ok = dialog.ok([&] {
        picked = std::ranges::views::iota(std::size_t{}, entries.size())
            | std::ranges::views::filter([&checked](std::size_t idx) { return checked[idx]; })  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
            | std::ranges::views::transform([&entries](std::size_t idx) { return entries.at(idx); })
            | std::ranges::to<std::vector<std::string>>();
        return !accept || accept(picked);
    });
    if (!dialog.run({.title = title, .text = text, .input = confirm_on_enter(Container::Vertical(std::move(boxes)), on_ok, false), .input_rows = rows_of(entries), .hint = kToggleHint}, on_ok)) {
        return std::nullopt;
    }
    return picked;
}

auto run_in_terminal(const std::vector<std::string>& argv, std::string_view input) noexcept -> std::int32_t {
    auto* screen = ScreenInteractive::Active();
    if (screen == nullptr) {
        return gucc::utils::run_interactive(argv, input);
    }
    std::int32_t status{-1};
    screen->WithRestoredIO([&] { status = gucc::utils::run_interactive(argv, input); })();
    return status;
}

void menu_loop(std::string_view text, const std::vector<MenuEntry>& entries) noexcept {
    const auto labels = entries | std::ranges::views::transform(&MenuEntry::label) | std::ranges::to<std::vector<std::string>>();

    DialogLoop dialog;
    std::int32_t selected{};
    auto option     = MenuOption::Vertical();
    option.on_enter = dialog.ok([&] { return !entries.at(static_cast<std::size_t>(selected)).action(); });
    dialog.run({.text = text, .input = Menu(&labels, &selected, option), .input_rows = rows_of(labels)}, option.on_enter);
}

Component wizard_menu_step(std::string_view label, const std::vector<std::string>& entries,
    std::int32_t* selected, std::function<void()> on_enter) noexcept {
    auto opt     = MenuOption::Vertical();
    opt.on_enter = std::move(on_enter);
    auto menu    = Menu(&entries, selected, opt);
    return Renderer(menu, [menu, lbl = std::string{label}] {
        return vbox({
            text(lbl) | bold,
            separator(),
            menu->Render() | vscroll_indicator | frame | flex,
        });
    });
}

Component wizard_input_step(std::string_view label, std::string* value,
    std::string_view placeholder, bool password, std::function<void()> on_enter) noexcept {
    InputOption opt;
    opt.password  = password;
    opt.multiline = false;
    opt.on_enter  = std::move(on_enter);
    auto input    = Input(value, std::string{placeholder}, opt);
    return Renderer(input, [input, lbl = std::string{label}] {
        return vbox({
            text(lbl) | bold,
            separator(),
            input->Render(),
        });
    });
}

}  // namespace tui::detail
