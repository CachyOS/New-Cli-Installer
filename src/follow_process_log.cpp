#include "follow_process_log.hpp"
#include "install_observers.hpp"
#include "installer_state.hpp"
#include "widgets.hpp"

// import gucc
#include "gucc/io_utils.hpp"
#include "gucc/process.hpp"

#include <cstddef>  // for size_t
#include <cstdint>  // for int32_t

#include <atomic>   // for atomic_bool
#include <deque>    // for deque
#include <mutex>    // for mutex, lock_guard
#include <ranges>   // for ranges::*
#include <string>   // for string
#include <thread>   // for jthread
#include <utility>  // for move
#include <variant>  // for get

#include <ftxui/component/component.hpp>          // for Renderer, Button
#include <ftxui/component/component_options.hpp>  // for ButtonOption
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>  // for ScreenInteractive
#include <ftxui/dom/elements.hpp>

#include <fmt/core.h>
#include <spdlog/spdlog.h>

using namespace ftxui;  // NOLINT

namespace {

constexpr std::string_view kDoneMarker{"----------DONE----------"};

// per-frame re-render cost
constexpr std::size_t kMaxLines{2000};

}  // namespace

namespace tui::detail {

auto follow_process_log_task(ProcessTask task, Decorator box_size) noexcept -> bool {
    auto& runner = gucc::utils::default_runner();
    runner.reset_cancel();

    std::atomic_bool task_status{true};
    std::atomic_bool task_done{false};

    std::mutex log_mtx;
    std::deque<std::string> lines;

    auto screen = ScreenInteractive::Fullscreen();

    const auto push_line = [&](std::string_view line) {
        {
            const std::lock_guard lk{log_mtx};
            lines.emplace_back(line);
            if (lines.size() > kMaxLines) {
                lines.pop_front();
            }
        }
        screen.PostEvent(Event::Custom);
    };
    LineObserver observer{push_line};
    const auto observer_handle = runner.add_observer(observer);

    std::jthread worker([&] {
        if (!task()) {
            spdlog::error("[follow] Task failed");
            task_status = false;
        }
        push_line(kDoneMarker);
        task_done = true;
        screen.PostEvent(Event::Custom);
    });

    auto handle_exit = [&] {
        if (!task_done) {
            // cancel in-flight work
            runner.cancel();
            return;
        }
        screen.ExitLoopClosure()();
    };

    auto button_back = Button("Back", handle_exit, ButtonOption::WithoutBorder());
    auto container   = Container::Horizontal({button_back});

    auto renderer = Renderer(container, [&] {
        std::vector<std::string> snapshot;
        {
            // newest first
            const std::lock_guard lk{log_mtx};
            snapshot.assign(lines.rbegin(), lines.rend());
        }
        return tui::detail::centered_widget(container, "New CLI Installer",
            tui::detail::multiline_text(std::move(snapshot)) | box_size | vscroll_indicator | yframe | flex);
    });

    screen.Loop(renderer);

    if (!task_done) {
        runner.cancel();
    }
    worker.join();
    return task_status;
}

auto follow_process_log_task_stdout(ProcessTask task) noexcept -> bool {
    auto& runner = gucc::utils::default_runner();
    runner.reset_cancel();

    ScriptStdoutObserver observer;
    const auto observer_handle = runner.add_observer(observer);

    const bool ok = task();
    if (!ok) {
        spdlog::error("[follow] Task failed");
    }
    return ok;
}

auto follow_task(ProcessTask task) noexcept -> bool {
    return tui::state().plain_progress ? follow_process_log_task_stdout(std::move(task)) : follow_process_log_task(std::move(task));
}

auto follow_process_log_widget(const std::vector<std::string>& vec, Decorator box_size) noexcept -> bool {
    return follow_process_log_task([&vec] { return gucc::utils::exec_checked(vec); }, box_size);
}

}  // namespace tui::detail
