#include "config.hpp"       // for Config
#include "definitions.hpp"  // for error_inter
#include "tui.hpp"          // for init
#include "utils.hpp"        // for exec, check_root, clear_sc...

#include <chrono>  // for seconds
#include <regex>   // for regex_search, match_result...
#include <thread>  // for sleep_for

#include <spdlog/async.h>                  // for create_async
#include <spdlog/common.h>                 // for debug
#include <spdlog/sinks/basic_file_sink.h>  // for basic_file_sink_mt
#include <spdlog/spdlog.h>                 // for set_default_logger, set_level

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
        error_inter("An active network connection could not be detected, please connect and restart the installer.");
        return 0;
    }

    auto logger = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>("cachyos_logger", "/tmp/cachyos-install.log");
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%r][%^---%L---%$] %v");
    spdlog::set_level(spdlog::level::debug);

    // auto app_router = std::make_shared<router>(tui::screen_service::instance());
    // app_router->navigate("", std::any());
    utils::parse_config();
    tui::init();

    spdlog::shutdown();
}
