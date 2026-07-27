#include "follow_process_log.hpp"
#include "widgets.hpp"

// import gucc
#include "gucc/process.hpp"
#include "gucc/string_utils.hpp"

#include <atomic>      // for atomic_bool
#include <mutex>       // for mutex, lock_guard
#include <stop_token>  // for stop_source, stop_token, stop_callback
#include <string>      // for string
#include <thread>      // for thread
#include <utility>     // for move

#include <ftxui/component/component.hpp>          // for Renderer, Button
#include <ftxui/component/component_options.hpp>  // for ButtonOption, Inpu...
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>  // for ScreenInteractive
#include <ftxui/dom/elements.hpp>                  // for operator|, text, Element, hbox, bold, color, filler, separator, vbox, window, gauge, Fit, size, dim, EQUAL, WIDTH

#include <fmt/core.h>
#include <spdlog/spdlog.h>

using namespace ftxui;  // NOLINT

namespace {

using tui::detail::ProcessTask;
using tui::detail::StepLogCallback;
using tui::detail::StepRunner;

constexpr std::string_view kDoneMarker{"----------DONE----------"};

auto as_step_runner(ProcessTask task) -> StepRunner {
    return [task = std::move(task)](StepLogCallback log_cb, std::stop_token stop_token) -> bool {
        auto& runner = gucc::utils::default_runner();
        runner.set_line_sink(std::move(log_cb));
        const std::stop_callback on_cancel(stop_token, [&runner] { runner.cancel(); });
        const bool ok = task();
        runner.set_line_sink(nullptr);
        return ok;
    };
}

}  // namespace

namespace tui::detail {

auto follow_process_log_widget(const std::vector<std::string>& vec, Decorator box_size) noexcept -> bool {
    return follow_process_log_task([&vec] {
        return gucc::utils::default_runner().run(vec).ok();
    },
        box_size);
}

auto follow_process_log_task(ProcessTask task, Decorator box_size) noexcept -> bool {
    return follow_step_widget(as_step_runner(std::move(task)), box_size);
}

auto follow_process_log_task_stdout(ProcessTask task) noexcept -> bool {
    return follow_step_stdout(as_step_runner(std::move(task)));
}

auto follow_step_widget(StepRunner runner, Decorator box_size) noexcept -> bool {
    gucc::utils::default_runner().reset_cancel();

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
        log_cb(kDoneMarker);
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
    gucc::utils::default_runner().reset_cancel();

    std::stop_source stop_src;
    auto log_cb = [](std::string_view line) {
        fmt::println("{}", line);
    };
    const bool ok = runner(log_cb, stop_src.get_token());
    if (!ok) {
        spdlog::error("[follow_step_stdout] Task failed");
    }
    log_cb(kDoneMarker);
    return ok;
}

}  // namespace tui::detail
