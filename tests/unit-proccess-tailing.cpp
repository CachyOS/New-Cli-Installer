#include "follow_process_log.hpp"

int main() {
    tui::detail::follow_process_log_widget({"/bin/sh", "-c", "sleep 1 && echo \"I slept\" && sleep 1 && echo \"I slept twice\""});
}
