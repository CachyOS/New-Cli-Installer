#include "definitions.hpp"
#include "utils.hpp"

#include <regex>
#include <thread>

#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using namespace ftxui;

static constexpr int32_t CONNECTION_TIMEOUT = 15;

void show_iwctl() {
    info("\nInstructions to connect to wifi using iwctl:\n");
    info("1 - To find your wifi device name (ex: wlan0) type `device list`\n");
    info("2 - type `station wlan0 scan`, and wait couple seconds\n");
    info("3 - type `station wlan0 get-networks` (find your wifi Network name ex. my_wifi)\n");
    info("4 - type `station wlan0 connect my_wifi` (don't forget to press TAB for auto completion!\n");
    info("5 - type `station wlan0 show` (status should be connected)\n");
    info("6 - type `exit`\n");
    while (utils::prompt_char("Press a key to continue...", CYAN)) {
        utils::exec("iwctl");
        break;
    }
}

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

    bool connected;

    if (!(connected = utils::is_connected())) {
        warning("An active network connection could not be detected, waiting 15 seconds ...\n");

        int32_t time_waited = 0;

        while (!(connected = utils::is_connected())) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            if (time_waited++ >= CONNECTION_TIMEOUT) {
                break;
            }
        }

        if (!connected) {
            char type = '\0';

            while (utils::prompt_char("An active network connection could not be detected, do you want to connect using wifi? [y/n]", RED, &type)) {
                if (type != 'n') {
                    show_iwctl();
                }

                break;
            }

            connected = utils::is_connected();
        }

        if (!connected) {
            error("An active network connection could not be detected, please connect and restart the installer.");
            return 0;
        }
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
