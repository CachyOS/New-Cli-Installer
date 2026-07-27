#include "follow_process_log.hpp"

#include "gucc/process.hpp"

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

int main() {
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);

    tui::detail::follow_process_log_widget({"/bin/sh", "-c", "sleep 1 && echo 'I slept' && sleep 1 && echo 'I slept twice'"});
    tui::detail::follow_process_log_task([]() -> bool {
        auto& runner = gucc::utils::default_runner();

        // Run first command
        if (!runner.run({"/bin/sh", "-c", "echo 'Starting first command...' && sleep 1 && echo 'First command finished.'"}).ok()) {
            return false;
        }

        // Run second command
        return runner.run({"/bin/sh", "-c", "echo 'Starting second command...' && sleep 1 && echo 'Second command finished.'"}).ok();
    });
}
