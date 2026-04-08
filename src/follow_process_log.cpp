#include "follow_process_log.hpp"
#include "widgets.hpp"

// import gucc
#include "gucc/string_utils.hpp"
#include "gucc/subprocess.hpp"

#include <atomic>      // for atomic_bool
#include <chrono>      // for chrono_literals
#include <mutex>       // for mutex, lock_guard
#include <stop_token>  // for stop_source, stop_token
#include <string>      // for string
#include <thread>      // for this_thread, thread
#include <utility>     // for move

#include <ftxui/component/component.hpp>          // for Renderer, Button
#include <ftxui/component/component_options.hpp>  // for ButtonOption, Inpu...
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>  // for ScreenInteractive
#include <ftxui/dom/elements.hpp>                  // for operator|, text, Element, hbox, bold, color, filler, separator, vbox, window, gauge, Fit, size, dim, EQUAL, WIDTH

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
        if (child.running) {
            // block exit while process is in-flight
            return;
        }
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
    // Non-interactive run: the subprocess output is funnelled through the logger
    // (gucc SubProcess::append_log) and emitted by whatever sinks are attached
    // (stdout in headless / simple mode). There is no ftxui screen to drive and
    // nothing to print here, so just run the task to completion.
    gucc::utils::SubProcess child{};
    const bool task_status = task(child);
    child.running          = false;
    if (!task_status) {
        spdlog::error("[follow_process_log_stdout] Task failed");
    }
    return task_status;
}

auto follow_step_widget(StepRunner runner, Decorator box_size) noexcept -> bool {
    using namespace std::chrono_literals;

    std::atomic_bool task_status{true};
    std::atomic_bool task_done{false};
    std::stop_source stop_src;

    std::mutex log_mtx;
    std::string accumulated;

    auto screen = ScreenInteractive::Fullscreen();

    auto log_cb = [&](std::string_view line) {
        {
            const std::lock_guard lk{log_mtx};
            accumulated.append(line);
            accumulated.push_back('\n');
        }
        screen.PostEvent(Event::Custom);
    };

    std::thread worker([&] {
        if (!runner(log_cb, stop_src.get_token())) {
            spdlog::error("[follow_step] Task failed");
            task_status = false;
        }
        task_done = true;
        screen.PostEvent(Event::Custom);
    });

    auto handle_exit = [&] {
        if (!task_done) {
            // cancel in-flight work
            stop_src.request_stop();
            return;
        }
        screen.ExitLoopClosure()();
    };

    auto button_back = Button("Back", handle_exit, ButtonOption::WithoutBorder());
    auto container   = Container::Horizontal({button_back});

    auto renderer = Renderer(container, [&] {
        std::string snapshot;
        {
            const std::lock_guard lk{log_mtx};
            snapshot = accumulated;
        }
        return tui::detail::centered_widget(container, "New CLI Installer",
            tui::detail::multiline_text(gucc::utils::make_multiline(snapshot, true)) | box_size | vscroll_indicator | yframe | flex);
    });

    screen.Loop(renderer);

    if (!task_done) {
        stop_src.request_stop();
    }
    if (worker.joinable()) {
        worker.join();
    }
    return task_status;
}

auto follow_step_stdout(StepRunner runner) noexcept -> bool {
    // Non-interactive run: log lines flow through the logger's sinks, so we only
    // drive the runner to completion with an empty per-line callback.
    std::stop_source stop_src;
    if (!runner(StepLogCallback{}, stop_src.get_token())) {
        spdlog::error("[follow_step_stdout] Task failed");
        return false;
    }
    return true;
}

}  // namespace tui::detail
