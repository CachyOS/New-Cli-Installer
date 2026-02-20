#include "follow_process_log.hpp"
#include "gucc/subprocess.hpp"

int main() {
    tui::detail::follow_process_log_widget({"/bin/sh", "-c", "sleep 1 && echo 'I slept' && sleep 1 && echo 'I slept twice'"});
    tui::detail::follow_process_log_task([](gucc::utils::SubProcess& child) -> bool {
        // Run first command
        bool first_ok = gucc::utils::exec_follow({"/bin/sh", "-c", "echo 'Starting first command...' && sleep 1 && echo 'First command finished.'"}, child);
        if (!first_ok) {
            return false;
        }

        // Run second command
        bool second_ok = gucc::utils::exec_follow({"/bin/sh", "-c", "echo 'Starting second command...' && sleep 1 && echo 'Second command finished.'"}, child);
        return second_ok;
    });
}

