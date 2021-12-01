#include "tui.hpp"
#include "definitions.hpp"
#include "utils.hpp"

#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

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

// Simple code to show devices / partitions.
void show_devices() {
    auto screen       = ScreenInteractive::Fullscreen();
    const auto& lsblk = utils::exec("lsblk -o NAME,MODEL,TYPE,FSTYPE,SIZE,MOUNTPOINT | grep \"disk\\|part\\|lvm\\|crypt\\|NAME\\|MODEL\\|TYPE\\|FSTYPE\\|SIZE\\|MOUNTPOINT\"");

    /* clang-format off */
    auto button_option   = ButtonOption();
    button_option.border = false;
    auto button_back     = Button("Back", screen.ExitLoopClosure(), &button_option);
    /* clang-format on */

    auto container = Container::Horizontal({
        button_back,
    });

    auto renderer = Renderer(container, [&] {
        return tui::centered_widget(container, "New CLI Installer", text(lsblk.data()) | size(HEIGHT, GREATER_THAN, 5));
    });

    screen.Loop(renderer);
}

void init() noexcept {
    auto screen = ScreenInteractive::Fullscreen();

    /* clang-format off */
    auto button_option   = ButtonOption();
    button_option.border = false;
    auto button_ok       = Button("OK", [=] { info("ok\n"); }, &button_option);
    auto button_quit     = Button("Quit", screen.ExitLoopClosure(), &button_option);
    /* clang-format on */

    auto container = Container::Horizontal({
        button_ok,
        Renderer([] { return filler() | size(WIDTH, GREATER_THAN, 3); }),
        button_quit,
    });

    auto renderer = Renderer(container, [&] {
        return tui::centered_widget(container, "New CLI Installer", text("TODO!!") | size(HEIGHT, GREATER_THAN, 5));
    });

    screen.Loop(renderer);
}
}  // namespace tui
