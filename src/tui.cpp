#include "tui.hpp"
#include "definitions.hpp"

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
        return vbox({
            //  -------- Title --------------
            text("New CLI Installer") | bold,
            filler(),
            //  -------- Center Menu --------------
            hbox({
                filler(),
                border(vbox({
                    text("TODO!!") | size(HEIGHT, GREATER_THAN, 5),
                    separator(),
                    container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25),
                })),
                filler(),
            }) | center,
            filler(),
        });
    });

    screen.Loop(renderer);
}
}  // namespace tui
