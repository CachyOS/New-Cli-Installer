#include "definitions.hpp"     // for error_inter
#include "global_storage.hpp"  // for Config
#include "tui.hpp"             // for init
#include "utils.hpp"           // for exec, check_root

// import cachyos
#include "cachyos/installer_config.hpp"
#include "cachyos/logging.hpp"
#include "cachyos/orchestrator.hpp"
#include "cachyos/session.hpp"
#include "cachyos/system.hpp"

// import gucc
#include "gucc/cpu.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/process.hpp"

#include "cli.hpp"

#include <chrono>       // for chrono_literals
#include <string>       // for string
#include <string_view>  // for string_view

#include <fmt/compile.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

int main(int argc, char** argv) {
    auto args = cli::parse(argc, argv);
    if (!args) {
        return 0;
    }

    const auto& config_path = args->config_path;
    // Initialize logger.
    cachyos::installer::logging::init();

    // Allow real execution via env var (legacy; --dry-run is the primary path).
#ifndef NDEVENV
    const bool force_real_run = gucc::utils::safe_getenv("DIRTY_CMD_RUN") == "1";
    if (force_real_run) {
        gucc::utils::default_runner().set_dry_run(false);
    }
#endif

    const auto tty = gucc::utils::exec("tty");
    if (tty.starts_with("/dev/tty")) {
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

    // Headless branch
    {
        const auto json = gucc::file_utils::read_whole_file(config_path);
        if (!json.empty()) {
            const auto parsed = cachyos::installer::parse_installer_config(json);
            if (parsed && parsed->headless_mode) {
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

                const cachyos::installer::InstallSession session{
                    .runner      = gucc::utils::default_runner(),
                    .on_progress = [](const cachyos::installer::ProgressEvent& evt) noexcept { fmt::print(stderr, "[{:>5.1f}%] {}\n", evt.fraction * 100.0, evt.message); },
                };

                spdlog::info("Running installer in headless mode");
                const auto result = cachyos::installer::run(
                    inputs->ctx, inputs->sys, inputs->user, inputs->root_password, session);

                // optional post-install script
                if (result.success && parsed->post_install && !parsed->post_install->empty()) {
                    spdlog::info("Running post-install script: {}", *parsed->post_install);
                    gucc::utils::default_runner().run_shell(fmt::format(FMT_COMPILE("{} &>>/tmp/cachyos-install.log"), *parsed->post_install));
                }

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

    if (!utils::parse_config(config_path)) {
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
