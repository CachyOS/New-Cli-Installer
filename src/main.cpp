#include "definitions.hpp"     // for error_inter
#include "global_storage.hpp"  // for Config
#include "tui.hpp"             // for init
#include "utils.hpp"           // for exec, check_root

// import cachyos
#include "cachyos/installer_config.hpp"
#include "cachyos/logging.hpp"
#include "cachyos/orchestrator.hpp"
#include "cachyos/system.hpp"

// import gucc
#include "gucc/cpu.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"

#include <chrono>       // for chrono_literals
#include <regex>        // for regex_search
#include <string_view>  // for string_view

#include <fmt/core.h>
#include <spdlog/spdlog.h>  // for info, warn, error, shutdown

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

    // Initialize the shared logger: one file sink, written identically in every
    // mode. Per-mode sinks (stdout / ftxui / GUI) are attached on top of it.
    cachyos::installer::logging::init();

    // Headless branch
    {
        const auto json = gucc::file_utils::read_whole_file("settings.json");
        if (!json.empty()) {
            const auto parsed = cachyos::installer::parse_installer_config(json);
            if (parsed && parsed->headless_mode) {
                // Stream the whole install to stdout like a shell script; the
                // file sink still captures the identical decorated log.
                cachyos::installer::logging::attach_stdout_sink();

                using namespace std::chrono_literals;
                if (!cachyos::installer::wait_for_connection(15s)) {
                    error_inter("An active network connection is required for headless install\n");
                    spdlog::shutdown();
                    return 1;
                }

                // print cpu info
                const auto& isa_levels = gucc::cpu::get_isa_levels();
                spdlog::info("isa_levels:={}", isa_levels);

                if (const auto repo = cachyos::installer::install_cachyos_repo(); !repo) {
                    spdlog::warn("install_cachyos_repo: {}", repo.error());
                }

                if (const auto v = cachyos::installer::validate_headless_config(*parsed); !v) {
                    error_inter("Headless config validation failed: {}\n", v.error());
                    spdlog::shutdown();
                    return -1;
                }
                auto inputs = cachyos::installer::installer_config_to_inputs(*parsed);
                if (!inputs) {
                    error_inter("Headless config conversion failed: {}\n", inputs.error());
                    spdlog::shutdown();
                    return -1;
                }

                // Log lines reach stdout via the sink above; only progress needs
                // an explicit callback (rendered to stderr, off the log stream).
                const cachyos::installer::ExecutionCallbacks callbacks{
                    .on_progress = [](const cachyos::installer::ProgressEvent& evt) noexcept { fmt::print(stderr, "[{:>5.1f}%] {}\n", evt.fraction * 100.0, evt.message); },
                };

                spdlog::info("Running installer in headless mode");
                const auto result = cachyos::installer::run(
                    inputs->ctx, inputs->sys, inputs->user, inputs->root_password, callbacks);

                for (const auto& warn : result.warnings) {
                    spdlog::warn("install warning: {}", warn);
                }
                for (const auto& err : result.errors) {
                    spdlog::error("install error: {}", err);
                }
                spdlog::shutdown();
                return result.success ? 0 : 1;
            }
        }
    }

    if (!utils::handle_connection()) {
        error_inter("An active network connection could not be detected, please connect and restart the installer.\n");
        return 0;
    }

    if (!utils::parse_config()) {
        error_inter("Error occurred during initialization! Closing installer..\n");
        spdlog::shutdown();
        return -1;
    }

    // print cpu info
    const auto& isa_levels = gucc::cpu::get_isa_levels();
    spdlog::info("isa_levels:={}", isa_levels);

    tui::init();

    spdlog::shutdown();
}
