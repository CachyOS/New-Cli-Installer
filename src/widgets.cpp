#include "widgets.hpp"
#include "config.hpp"
#include "utils.hpp"

/* clang-format off */
#include <algorithm>                               // for transform
#include <string>                                  // for basic_string
#include <ftxui/component/component.hpp>           // for Renderer, Button
#include <ftxui/component/component_options.hpp>   // for ButtonOption
#include <ftxui/component/screen_interactive.hpp>  // for Component, ScreenI...
/* clang-format on */

using namespace ftxui;

namespace tui::detail {

Element centered_widget(Component& container, const std::string_view& title, const Element& widget) noexcept {
    return vbox({
        //  -------- Title --------------
        text(title.data()) | bold,
        filler(),
        //  -------- Center Menu --------------
        hbox({
            filler(),
            border(vbox({
                widget,
                separator(),
                container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25),
            })),
            filler(),
        }) | center,
        filler(),
    });
}

Element centered_widget_nocontrols(const std::string_view& title, const Element& widget) noexcept {
    return vbox({
        //  -------- Title --------------
        text(title.data()) | bold,
        filler(),
        //  -------- Center Menu --------------
        hbox({
            filler(),
            border(vbox({widget})),
            filler(),
        }) | center,
        filler(),
    });
}

Component controls_widget(const std::array<std::string_view, 2>&& titles, const std::array<std::function<void()>, 2>&& callbacks, ButtonOption* button_option) noexcept {
    /* clang-format off */
    auto button_ok       = Button(titles[0].data(), callbacks[0], button_option);
    auto button_quit     = Button(titles[1].data(), callbacks[1], button_option);
    /* clang-format on */

    auto container = Container::Horizontal({
        button_ok,
        Renderer([] { return filler() | size(WIDTH, GREATER_THAN, 3); }),
        button_quit,
    });

    return container;
}

Element centered_interative_multi(const std::string_view& title, Component& widgets) noexcept {
    return vbox({
        //  -------- Title --------------
        text(title.data()) | bold,
        filler(),
        //  -------- Center Menu --------------
        hbox({
            filler(),
            border(vbox({
                widgets->Render(),
            })),
            filler(),
        }) | center,
        filler(),
    });
}

Element multiline_text(const std::vector<std::string>& lines) noexcept {
    Elements multiline;

    std::transform(lines.cbegin(), lines.cend(), std::back_inserter(multiline),
        [=](const std::string& line) -> Element { return text(line); });
    return vbox(std::move(multiline)) | frame;
}

Components from_vector_checklist(const std::vector<std::string>& opts, bool* opts_state) noexcept {
    Components components;

    for (size_t i = 0; i < opts.size(); ++i) {
        auto component = Checkbox(&opts[i], &opts_state[i]);
        components.emplace_back(component);
    }
    return components;
}

std::string from_checklist_string(const std::vector<std::string>& opts, bool* opts_state) noexcept {
    std::string res{};

    for (size_t i = 0; i < opts.size(); ++i) {
        if (opts_state[i]) {
            res += opts[i] + " ";
        }
    }
    /* clang-format off */
    if (res.ends_with(" ")) { res.pop_back(); }
    /* clang-format on */
    return res;
}

std::vector<std::string> from_checklist_vector(const std::vector<std::string>& opts, bool* opts_state) noexcept {
    std::vector<std::string> res{};

    for (size_t i = 0; i < opts.size(); ++i) {
        if (opts_state[i]) {
            res.push_back(opts[i]);
        }
    }
    return res;
}

void msgbox_widget(const std::string_view& content, Decorator boxsize) noexcept {
    auto screen = ScreenInteractive::Fullscreen();
    /* clang-format off */
    auto button_option   = ButtonOption();
    button_option.border = false;
    auto button_back     = Button("OK", screen.ExitLoopClosure(), &button_option);
    /* clang-format on */

    auto container = Container::Horizontal({
        button_back,
    });

    auto renderer = Renderer(container, [&] {
        return centered_widget(container, "New CLI Installer", multiline_text(utils::make_multiline(content.data())) | hcenter | boxsize);
    });

    screen.Loop(renderer);
}

bool inputbox_widget(std::string& value, const std::string_view& content, Decorator boxsize, bool password) noexcept {
    auto screen = ScreenInteractive::Fullscreen();
    bool success{};
    auto ok_callback = [&] {
        success = true;
        screen.ExitLoopClosure()();
    };
    InputOption input_option{.on_enter = ok_callback, .password = password};
    auto input_value       = Input(&value, "", input_option);
    auto content_container = Renderer([&] {
        return multiline_text(utils::make_multiline(content)) | hcenter | boxsize;
    });

    ButtonOption button_option{.border = false};
    auto controls_container = controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    auto global = Container::Vertical({
        content_container,
        Renderer([] { return separator(); }),
        input_value,
        Renderer([] { return separator(); }),
        controls,
    });

    auto renderer = Renderer(global, [&] {
        return centered_interative_multi("New CLI Installer", global);
    });

    screen.Loop(renderer);
    return success;
}

void infobox_widget(const std::string_view& content, Decorator boxsize) noexcept {
    auto screen = Screen::Create(
        Dimension::Full(),  // Width
        Dimension::Full()   // Height
    );

    auto element = centered_widget_nocontrols("New CLI Installer", multiline_text(utils::make_multiline(content.data())) | vcenter | boxsize);
    Render(screen, element);
    screen.Print();
}

bool yesno_widget(const std::string_view& content, Decorator boxsize) noexcept {
    auto screen = ScreenInteractive::Fullscreen();

    bool success{};
    auto ok_callback = [&] {
        success = true;
        screen.ExitLoopClosure()();
    };
    ButtonOption button_option{.border = false};
    auto controls_container = controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    auto container = Container::Horizontal({
        controls,
    });

    auto renderer = Renderer(container, [&] {
        return centered_widget(container, "New CLI Installer", multiline_text(utils::make_multiline(content.data())) | hcenter | boxsize);
    });

    screen.Loop(renderer);
    return success;
}

bool yesno_widget(ftxui::Component& container, Decorator boxsize) noexcept {
    auto screen = ScreenInteractive::Fullscreen();

    auto content = Renderer(container, [&] {
        return container->Render() | hcenter | boxsize;
    });

    bool success{};
    auto ok_callback = [&] {
        success = true;
        screen.ExitLoopClosure()();
    };
    ButtonOption button_option{.border = false};
    auto controls_container = controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    auto global = Container::Vertical({
        content,
        Renderer([] { return separator(); }),
        controls,
    });

    auto renderer = Renderer(global, [&] {
        return centered_interative_multi("New CLI Installer", global);
    });

    screen.Loop(renderer);
    return success;
}

void menu_widget(const std::vector<std::string>& entries, const std::function<void()>&& ok_callback, std::int32_t* selected, ScreenInteractive* screen, const std::string_view& text, const ContentBoxSize content_size) noexcept {
    MenuOption menu_option{.on_enter = ok_callback};
    auto menu    = Menu(&entries, selected, &menu_option);
    auto content = Renderer(menu, [&] {
        return menu->Render() | vscroll_indicator | center | size(HEIGHT, GREATER_THAN, content_size.height) | size(WIDTH, GREATER_THAN, content_size.width);
    });

    ButtonOption button_option{.border = false};
    auto controls_container = controls_widget({"OK", "Cancel"}, {ok_callback, screen->ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    Components children{};
    if (!text.empty()) {
        children = {
            Renderer([&] { return detail::multiline_text(utils::make_multiline(text)) | content_size.text_size; }),
            Renderer([] { return separator(); }),
            content,
            Renderer([] { return separator(); }),
            controls};
    } else {
        children = {
            content,
            Renderer([] { return separator(); }),
            controls};
    }
    auto global{Container::Vertical(children)};

    auto renderer = Renderer(global, [&] {
        return centered_interative_multi("New CLI Installer", global);
    });

    screen->Loop(renderer);
}

}  // namespace tui::detail
