#include "config.hpp"
#include "definitions.hpp"
#include "screen_service.hpp"
#include "tui.hpp"
#include "utils.hpp"

#include <chrono>  // for seconds
#include <regex>   // for regex_search, match_results<>::_Base_type
#include <thread>  // for sleep_for

int main() {
    const auto& tty = utils::exec("tty");
    const std::regex tty_regex("/dev/tty[0-9]*");
    if (std::regex_search(tty, tty_regex)) {
        utils::exec("setterm -blank 0 -powersave off");
    }

    if (!utils::check_root()) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        utils::clear_screen();
        return 1;
    }

    if (!Config::initialize()) {
        return 1;
    }

    utils::id_system();

    if (!utils::handle_connection()) {
        error("An active network connection could not be detected, please connect and restart the installer.");
        return 0;
    }

    // auto app_router = std::make_shared<router>(tui::screen_service::instance());
    // app_router->navigate("", std::any());
    tui::init();
}
