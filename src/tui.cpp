#include "tui.hpp"
#include "config.hpp"
#include "definitions.hpp"
#include "utils.hpp"

/* clang-format off */
#include <algorithm>                               // for transform
#include <memory>                                  // for __shared_ptr_access
#include <string>                                  // for basic_string
#include <ftxui/component/captured_mouse.hpp>      // for ftxui
#include <ftxui/component/component.hpp>           // for Renderer, Button
#include <ftxui/component/component_options.hpp>   // for ButtonOption
#include <ftxui/component/screen_interactive.hpp>  // for Component, ScreenI...
#include <ftxui/dom/elements.hpp>                  // for operator|, size
/* clang-format on */

using namespace ftxui;

namespace tui {

ftxui::Element centered_widget(ftxui::Component& container, const std::string_view& title, const ftxui::Element& widget) {
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

ftxui::Component controls_widget(const std::array<std::string_view, 2>&& titles, const std::array<std::function<void()>, 2>&& callbacks) {
    /* clang-format off */
    auto button_option   = ButtonOption();
    button_option.border = false;
    auto button_ok       = Button(titles[0].data(), callbacks[0], &button_option);
    auto button_quit     = Button(titles[1].data(), callbacks[1], &button_option);
    /* clang-format on */

    auto container = Container::Horizontal({
        button_ok,
        Renderer([] { return filler() | size(WIDTH, GREATER_THAN, 3); }),
        button_quit,
    });

    return container;
}

ftxui::Element centered_interative_multi(const std::string_view& title, ftxui::Component& widgets) {
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

ftxui::Element multiline_text(const std::vector<std::string>& lines) {
    Elements multiline;

    std::transform(lines.cbegin(), lines.cend(), std::back_inserter(multiline),
        [=](const std::string& line) -> Element { return text(line); });
    return vbox(std::move(multiline)) | frame;
}

// Simple code to show devices / partitions.
void show_devices() noexcept {
    auto screen = ScreenInteractive::Fullscreen();
    auto lsblk  = utils::exec("lsblk -o NAME,MODEL,TYPE,FSTYPE,SIZE,MOUNTPOINT | grep \"disk\\|part\\|lvm\\|crypt\\|NAME\\|MODEL\\|TYPE\\|FSTYPE\\|SIZE\\|MOUNTPOINT\"");

    /* clang-format off */
    auto button_option   = ButtonOption();
    button_option.border = false;
    auto button_back     = Button("Back", screen.ExitLoopClosure(), &button_option);
    /* clang-format on */

    auto container = Container::Horizontal({
        button_back,
    });

    auto renderer = Renderer(container, [&] {
        return tui::centered_widget(container, "New CLI Installer", multiline_text(utils::make_multiline(lsblk)) | size(HEIGHT, GREATER_THAN, 5));
    });

    screen.Loop(renderer);
}

// This function does not assume that the formatted device is the Root installation device as
// more than one device may be formatted. Root is set in the mount_partitions function.
void select_device() noexcept {
    auto* config_instance    = Config::instance();
    auto& config_data        = config_instance->data();
    auto devices             = utils::exec("lsblk -lno NAME,SIZE,TYPE | grep 'disk' | awk '{print \"/dev/\" $1 \" \" $2}' | sort -u");
    const auto& devices_list = utils::make_multiline(devices);

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    auto menu    = Menu(&devices_list, &selected);
    auto content = Renderer(menu, [&] {
        return menu->Render() | center | size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    });

    auto ok_callback        = [&] { config_data["DEVICE"] = devices_list[selected]; };
    auto controls_container = controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()});

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    auto global = Container::Vertical({
        content,
        Renderer([] { return separator(); }),
        controls,
    });

    auto renderer = Renderer(global, [&] {
        return tui::centered_interative_multi("New CLI Installer", global);
    });

    screen.Loop(renderer);
}

void init() noexcept {
    auto screen      = ScreenInteractive::Fullscreen();
    auto ok_callback = [=] { info("ok\n"); };
    auto container   = controls_widget({"OK", "Quit"}, {ok_callback, screen.ExitLoopClosure()});

    auto renderer = Renderer(container, [&] {
        return tui::centered_widget(container, "New CLI Installer", text("TODO!!") | size(HEIGHT, GREATER_THAN, 5));
    });

    screen.Loop(renderer);
}
}  // namespace tui
