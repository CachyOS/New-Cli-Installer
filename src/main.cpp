#include "definitions.hpp"
#include "install_observers.hpp"
#include "installer_state.hpp"
#include "tui.hpp"
#include "utils.hpp"

// import cachyos
#include "cachyos/installer_config.hpp"
#include "cachyos/logging.hpp"
#include "cachyos/run_from_config.hpp"
#include "cachyos/session.hpp"
#include "cachyos/system.hpp"

// import gucc
#include "gucc/cpu.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/process.hpp"
#include "gucc/string_utils.hpp"

#include <unistd.h>  // for ttyname, STDIN_FILENO

#include <string>       // for string
#include <string_view>  // for string_view

#include <fmt/compile.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

// TODO(vnepogodin): refactor using argparse
constexpr std::string_view kUsageMsg = R"(
Usage: cachyos-installer [--config <path>] [--dry-run] [--version]

  --config <path>  Read installer config from <path> (default: ./settings.json).
                   A config with "headless_mode": true installs unattended;
                   otherwise the interactive TUI starts.
  --dry-run        Log every system-changing command instead of running it.
  --version        Print version and exit.
  --help           Show this help and exit.

Must be run as root with an active network connection.
)";

int main(int argc, char** argv) {
    std::string config_path{"settings.json"};
    bool dry_run{false};
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--help"sv || arg == "-h"sv) {
            fmt::print("{}", kUsageMsg);
            return 0;
        }
        if (arg == "--version"sv || arg == "-v"sv) {
            fmt::print("cachyos-installer {}\n", INSTALLER_VERSION);
            return 0;
        }
        if (arg == "--dry-run"sv) {
            dry_run = true;
        } else if (arg == "--config"sv) {
            if (i + 1 >= argc) {
                fmt::print(stderr, "--config requires a path argument\n");
                return 1;
            }
            config_path = argv[++i];
        } else if (const auto path = gucc::utils::strip_prefix(arg, "--config="sv)) {
            config_path = *path;
        } else {
            fmt::print(stderr, "unknown argument '{}' (try --help)\n", arg);
            return 1;
        }
    }

    // shared logger needs to be installed in own registry
    spdlog::set_default_logger(cachyos::installer::logging::init());

#ifndef NDEVENV
    // dev builds never touch the system unless explicitly forced.
    dry_run = dry_run || gucc::utils::safe_getenv("DIRTY_CMD_RUN") != "1";
#endif
    gucc::utils::default_runner().set_dry_run(dry_run);

    // setterm writes escape codes, so it must run on the real terminal
    if (const char* tty = ::ttyname(STDIN_FILENO); tty != nullptr && std::string_view{tty}.starts_with("/dev/tty")) {
        gucc::utils::run_interactive({"/usr/bin/setterm"s, "-blank"s, "0"s, "-powersave"s, "off"s});
    }

    const auto config = utils::parse_config(config_path);
    if (!config) {
        error_inter("Invalid config '{}': {}\n", config_path, config.error());
        return 1;
    }
    if (config->headless_mode) {
        if (!dry_run && !utils::check_root()) {
            error_inter("Installer must be launched with root privileges!\n");
            return 1;
        }
        cachyos::installer::logging::attach_stdout_sink();

        cachyos::installer::InstallSession session{gucc::utils::default_runner()};
        tui::detail::ScriptStdoutObserver script_output;
        const auto output_handle = session.add_observer(script_output);

        spdlog::info("Running installer in headless mode");
        const auto result = cachyos::installer::run_from_config(*config, session);
        for (const auto& warn : result.warnings) {
            spdlog::warn("install warning: {}", warn);
        }
        for (const auto& err : result.errors) {
            spdlog::error("install error: {}", err);
        }
        spdlog::shutdown();
        return result.success ? 0 : 1;
    }

    // Check if installer has enough permissions.
    if (!utils::check_root()) {
        error_inter("Installer must be launched with root privileges!\n");
        return 1;
    }

    tui::init_state(*config);

    // Detect system information.
    // e.g UEFI/BIOS, APPLE, INIT
    utils::id_system();

    if (!utils::handle_connection()) {
        error_inter("An active network connection could not be detected, please connect and restart the installer.\n");
        return 0;
    }

    // version and server mode logged earlier
    spdlog::info("SYSTEM: '{}'", tui::state().inputs.ctx.is_uefi() ? "UEFI" : "BIOS");

    // print cpu info
    const auto& isa_levels = gucc::cpu::get_isa_levels();
    spdlog::info("isa_levels:={}", isa_levels);

    tui::init();

    spdlog::shutdown();
}
