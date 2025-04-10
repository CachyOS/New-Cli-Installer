#include "follow_process_log.hpp"
#include "subprocess.h"
#include "utils.hpp"  // for exec_follow
#include "widgets.hpp"

// import gucc
#include "gucc/string_utils.hpp"

#include <atomic>  // for atomic_bool
#include <chrono>  // for chrono_literals
#include <string>  // for string, operator<<, to_string
#include <thread>  // for this_thread, thread

#include <ftxui/component/component.hpp>          // for Renderer, Button
#include <ftxui/component/component_options.hpp>  // for ButtonOption, Inpu...
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>  // for ScreenInteractive
#include <ftxui/dom/elements.hpp>                  // for operator|, text, Element, hbox, bold, color, filler, separator, vbox, window, gauge, Fit, size, dim, EQUAL, WIDTH

using namespace ftxui;

namespace tui::detail {

auto follow_process_log_widget(const std::vector<std::string>& vec, Decorator box_size) noexcept -> bool {
    using namespace std::chrono_literals;

    std::vector<std::string> cmd_args(vec.begin(), vec.end());
    const auto cmd_args_size = cmd_args.size();
    if (cmd_args_size > 2) {
        const auto& second_arg = cmd_args[1];
        if (second_arg == "-c") {
            auto& end_arg = cmd_args.back();
            end_arg += " ; echo -e \"\n----------DONE----------\n\"";
        }
    }

    std::atomic_bool running{true};
    std::atomic_bool proc_status{true};
    std::string process_log{};
    subprocess_s child{};
    auto execute_thread = [&]() {
        while (running) {
            bool local_running{running};
            if (!utils::exec_follow(cmd_args, process_log, local_running, child)) {
                spdlog::error("[follow_process_log] Failed to exec proc");
                proc_status = false;
            }
            running = false;
            std::this_thread::sleep_for(0.05s);
        }
    };
    std::thread t(execute_thread);
    auto screen = ScreenInteractive::Fullscreen();

    std::thread refresh_ui([&] {
        while (running) {
            std::this_thread::sleep_for(0.05s);
            screen.PostEvent(Event::Custom);
        }
    });

    auto handle_exit = [&]() {
        if (child.child != 0) {
            subprocess_terminate(&child);
            subprocess_destroy(&child);
        }
        if (running) {
            running = false;
            if (refresh_ui.joinable()) {
                refresh_ui.join();
            }
            if (t.joinable()) {
                t.join();
            }
        }
        screen.ExitLoopClosure()();
    };

    auto button_back = Button("Back", handle_exit, ButtonOption::WithoutBorder());
    auto container   = Container::Horizontal({button_back});

    auto renderer = Renderer(container, [&] {
        return tui::detail::centered_widget(container, "New CLI Installer", tui::detail::multiline_text(gucc::utils::make_multiline(process_log, true)) | box_size | vscroll_indicator | yframe | flex);
    });

    screen.Loop(renderer);
    running = false;
    if (refresh_ui.joinable()) {
        refresh_ui.join();
    }
    if (t.joinable()) {
        t.join();
    }
    return proc_status;
}

}  // namespace tui::detail
