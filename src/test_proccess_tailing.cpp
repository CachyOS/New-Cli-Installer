#include <cstdio>
#include <thread>

#include <chrono>                                  // for operator""s, chrono_literals
#include <ftxui/component/component.hpp>           // for Renderer, Button
#include <ftxui/component/component_options.hpp>   // for ButtonOption, Inpu...
#include <ftxui/component/screen_interactive.hpp>  // for ScreenInteractive
#include <ftxui/dom/elements.hpp>                  // for operator|, text, Element, hbox, bold, color, filler, separator, vbox, window, gauge, Fit, size, dim, EQUAL, WIDTH
#include <ftxui/screen/screen.hpp>                 // for Full, Screen
#include <iostream>                                // for cout, endl, ostream
#include <list>                                    // for list, operator!=, _List_iterator, _List_iterator<>::_Self
#include <memory>                                  // for allocator, shared_ptr, allocator_traits<>::value_type
#include <string>                                  // for string, operator<<, to_string
#include <thread>                                  // for sleep_for
#include <utility>                                 // for move
#include <vector>                                  // for vector

#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/node.hpp>      // for Render
#include <ftxui/screen/box.hpp>    // for ftxui
#include <ftxui/screen/color.hpp>  // for Color, Color::Green, Color::Red, Color::RedLight

#include "subprocess.h"
#include "utils.hpp"
#include "widgets.hpp"

static bool running{true};
static std::string process_log{};

using namespace std::chrono_literals;

void execute() {
    const char* const commandLine[] = {"/sbin/tail", "-f", "/tmp/smth.log", nullptr};
    struct subprocess_s process;
    int ret                       = -1;
    static char data[1048576 + 1] = {0};
    unsigned index                = 0;
    unsigned bytes_read           = 0;

    if ((ret = subprocess_create(commandLine, subprocess_option_enable_async | subprocess_option_combined_stdout_stderr, &process)) != 0) {
        std::perror("creation failed");
        std::cerr << "creation failed with: " << ret << '\n';
        running = false;
        return;
    }

    do {
        bytes_read = subprocess_read_stdout(&process, data + index,
            sizeof(data) - 1 - index);

        index += bytes_read;

        process_log = data;
    } while (bytes_read != 0);

    subprocess_join(&process, &ret);
    subprocess_destroy(&process);
}

void execute_thread() {
    while (running) {
        execute();

        std::this_thread::sleep_for(2s);
    }
}

int main() {
    using namespace ftxui;
    std::jthread t(execute_thread);

    auto screen = ScreenInteractive::Fullscreen();

    std::jthread refresh_ui([&] {
        while (running) {
            std::this_thread::sleep_for(0.05s);
            screen.PostEvent(Event::Custom);
        }
    });

    auto button_option   = ButtonOption();
    button_option.border = false;
    auto button_back     = Button("Back", screen.ExitLoopClosure(), &button_option);
    auto container       = Container::Horizontal({button_back});

    auto renderer = Renderer(container, [&] {
        return tui::detail::centered_widget(container, "New CLI Installer", tui::detail::multiline_text(utils::make_multiline(process_log)) | vscroll_indicator | yframe | flex);
    });

    screen.Loop(renderer);
    running = false;
    t.join();
    refresh_ui.join();
}
