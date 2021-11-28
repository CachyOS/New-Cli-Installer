#include "definitions.hpp"
#include "tui.hpp"
#include "utils.hpp"

#include <regex>

int main() {
    const auto& tty = utils::exec("tty");
    const std::regex tty_regex("/dev/tty[0-9]*");
    if (std::regex_search(tty, tty_regex)) {
        utils::exec("setterm -blank 0 -powersave off");
    }

#ifdef NDEBUG
    if (!utils::check_root()) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        utils::clear_screen();
        return 1;
    }
#endif

    utils::id_system();

    if (!utils::handle_connection()) {
        error("An active network connection could not be detected, please connect and restart the installer.");
        return 0;
    }

    tui::init();
}
