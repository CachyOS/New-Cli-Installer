#include "follow_process_log.hpp"
#include "widgets.hpp"

// import gucc
#include "gucc/string_utils.hpp"
#include "gucc/subprocess.hpp"

#include <atomic>  // for atomic_bool
#include <chrono>  // for chrono_literals
#include <string>  // for string, operator<<, to_string
#include <thread>  // for this_thread, thread

#include <ftxui/component/component.hpp>          // for Renderer, Button
#include <ftxui/component/component_options.hpp>  // for ButtonOption, Inpu...
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>  // for ScreenInteractive
#include <ftxui/dom/elements.hpp>                  // for operator|, text, Element, hbox, bold, color, filler, separator, vbox, window, gauge, Fit, size, dim, EQUAL, WIDTH

#include <fmt/core.h>
#include <spdlog/spdlog.h>

using namespace ftxui;  // NOLINT

namespace tui::detail {

auto follow_process_log_widget(const std::vector<std::string>& vec, Decorator box_size) noexcept -> bool {
    // Wrap the single command as a ProcessTask and delegate.
    return follow_process_log_task([&](gucc::utils::SubProcess& child) -> bool {
        return gucc::utils::exec_follow(vec, child);
    },
        box_size);
}

auto follow_process_log_task(ProcessTask task, Decorator box_size) noexcept -> bool {
    using namespace std::chrono_literals;

    std::atomic_bool task_status{true};
    gucc::utils::SubProcess child{};

    auto screen = ScreenInteractive::Fullscreen();

    auto execute_thread = [&]() {
        if (!task(child)) {
            spdlog::error("[follow_process_log] Task failed");
            task_status = false;
        }
        child.running = false;
        screen.PostEvent(Event::Custom);
    };
    std::thread t(execute_thread);

    std::thread refresh_ui([&] {
        while (child.running) {
            std::this_thread::sleep_for(0.05s);
            screen.PostEvent(Event::Custom);
        }
    });

    auto handle_exit = [&]() {
        if (child.has_child()) {
            child.terminate();
            child.destroy();
        }
        if (child.running) {
            child.running = false;
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
        return tui::detail::centered_widget(container, "New CLI Installer", tui::detail::multiline_text(gucc::utils::make_multiline(child.get_log(), true)) | box_size | vscroll_indicator | yframe | flex);
    });

    screen.Loop(renderer);
    child.running = false;
    if (refresh_ui.joinable()) {
        refresh_ui.join();
    }
    if (t.joinable()) {
        t.join();
    }
    return task_status;
}

auto follow_process_log_task_stdout(ProcessTask task) noexcept -> bool {
    using namespace std::chrono_literals;

    std::atomic_bool task_status{true};
    gucc::utils::SubProcess child{};

    std::thread t([&]() {
        if (!task(child)) {
            spdlog::error("[follow_process_log_stdout] Task failed");
            task_status = false;
        }
        child.running = false;
    });

    std::string::size_type last_printed{};
    while (child.running) {
        std::this_thread::sleep_for(0.05s);
        const auto& log = child.get_log();

        // NOTE: hackish
        if (log.size() > last_printed) {
            fmt::print("{}", std::string_view{log}.substr(last_printed));
            last_printed = log.size();
        }
    }

    const auto& log = child.get_log();
    if (log.size() > last_printed) {
        fmt::print("{}", std::string_view{log}.substr(last_printed));
    }

    if (t.joinable()) {
        t.join();
    }
    return task_status;
}

}  // namespace tui::detail
