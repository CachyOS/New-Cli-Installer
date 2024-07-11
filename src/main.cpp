#include "config.hpp"       // for Config
#include "definitions.hpp"  // for error_inter
#include "tui.hpp"          // for init
#include "utils.hpp"        // for exec, check_root, clear_sc...

// import gucc
#include "gucc/io_utils.hpp"
#ifndef COS_BUILD_STATIC
#include "gucc/logger.hpp"
#endif

#include <chrono>  // for seconds
#include <regex>   // for regex_search, match_result...
#include <thread>  // for sleep_for

#include <spdlog/async.h>                  // for create_async
#include <spdlog/common.h>                 // for debug
#include <spdlog/sinks/basic_file_sink.h>  // for basic_file_sink_mt
#include <spdlog/spdlog.h>                 // for set_default_logger, set_level

int main() {
    const auto& tty = gucc::utils::exec("tty");
    const std::regex tty_regex("/dev/tty[0-9]*");
    if (std::regex_search(tty, tty_regex)) {
        gucc::utils::exec("setterm -blank 0 -powersave off");
    }

    // Check if installer has enough permissions.
    if (!utils::check_root()) {
        error_inter("Installer must be launched with root privileges!\n");
        return 1;
    }

    // Initialize default config.
    if (!Config::initialize()) {
        return 1;
    }

    // Detect system information.
    // e.g UEFI/BIOS, APPLE, INIT
    utils::id_system();

    // Initialize logger.
    auto logger = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>("cachyos_logger", "/tmp/cachyos-install.log");
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%r][%^---%L---%$] %v");
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_every(std::chrono::seconds(5));

#ifndef COS_BUILD_STATIC
    // Set gucc logger.
    gucc::logger::set_logger(logger);
#endif

    if (!utils::handle_connection()) {
        error_inter("An active network connection could not be detected, please connect and restart the installer.\n");
        return 0;
    }

    // auto app_router = std::make_shared<router>(tui::screen_service::instance());
    // app_router->navigate("", std::any());
    if (!utils::parse_config()) {
        error_inter("Error occurred during initialization! Closing installer..\n");
        spdlog::shutdown();
        return -1;
    }
    tui::init();

    spdlog::shutdown();
}
