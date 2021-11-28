#include "tui.hpp"
#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using namespace ftxui;

namespace tui {
void init() noexcept {
    auto screen = ScreenInteractive::Fullscreen();

    auto button_ok   = Button("Ok", screen.ExitLoopClosure());
    auto button_quit = Button("Quit", screen.ExitLoopClosure());

    auto component = Container::Horizontal({
        button_ok,
        button_quit,
    });

    auto dialog = vbox({
        text("TODO!!"),
        separator(),
        hbox({
            button_ok->Render(),
            button_quit->Render(),
        }),
    });

    // -------- Center Menu --------------
    /* clang-format off */
    auto center_dialog = hbox({
        filler(),
        border(dialog),
        filler(),
    }) | vcenter;
    /* clang-format on */

    auto renderer = Renderer(component, [&] {
        return vbox({
            text("New CLI Installer") | bold,
            filler(),
            //  -------- Center Menu --------------
            center_dialog | hcenter,
            filler(),
        });
    });

    screen.Loop(renderer);
}
}  // namespace tui
