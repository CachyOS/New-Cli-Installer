#include "definitions.hpp"
#include "utils.hpp"

#include <regex>

#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using namespace ftxui;

int main() {
    const auto& tty = utils::exec("tty");
    const std::regex tty_regex("/dev/tty[0-9]*");
    if (std::regex_search(tty, tty_regex)) {
        utils::exec("setterm -blank 0 -powersave off");
    }

#ifdef NDEBUG
    if (!utils::check_root()) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        utils::clear_screen();
        return 1;
    }
#endif

    utils::id_system();

    if (!utils::handle_connection()) {
        error("An active network connection could not be detected, please connect and restart the installer.");
        return 0;
    }

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
