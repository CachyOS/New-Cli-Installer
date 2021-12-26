#include "follow_process_log.hpp"

int main() {
    tui::detail::follow_process_log_widget({"/sbin/tail", "-f", "/tmp/smth.log"});
}
