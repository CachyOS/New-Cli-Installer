#include "cli.hpp"

#include <argparse/argparse.hpp>
#include <fmt/format.h>
#include <gucc/process.hpp>
#include <spdlog/spdlog.h>

#include <iostream>

namespace {

auto do_parse(int argc, char** argv) noexcept -> std::optional<cli::Args> {
    argparse::ArgumentParser program("cachyos-installer", INSTALLER_VERSION);

    // Override argparse's auto --help (which calls exit(0)) with our own that returns.
    program.add_argument("--help")
        .action([&](const auto&) {
            std::cout << program << "\n";
        })
        .default_value(false)
        .implicit_value(true)
        .nargs(0);
    // Override argparse's auto --version (which calls exit(0)).
    program.add_argument("--version")
        .action([&](const auto&) {
            std::cout << "cachyos-installer " << INSTALLER_VERSION << "\n";
        })
        .default_value(false)
        .implicit_value(true)
        .nargs(0);

    program.add_argument("--config")
        .default_value(std::string{"settings.json"})
        .help("Read installer config from <path> (default: ./settings.json). "
              "A config with \"headless_mode\": true installs unattended; "
              "otherwise the interactive TUI starts.");

    program.add_argument("--dry-run")
        .default_value(false)
        .implicit_value(true)
        .nargs(0)
        .help("Log commands instead of running them (debug builds only).");

    program.add_argument("--verbose")
        .default_value(false)
        .implicit_value(true)
        .nargs(0)
        .help("Enable debug-level logging.");

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& err) {
        std::cerr << "Error: " << err.what() << "\n\n" << program << "\n";
        return std::nullopt;
    }

    if (program.is_used("--help")) {
        std::cout << program << "\n";
        return std::nullopt;
    }
    if (program.is_used("--version")) {
        std::cout << "cachyos-installer " << INSTALLER_VERSION << "\n";
        return std::nullopt;
    }

    cli::Args args;
    args.config_path = program.get<std::string>("--config");
    args.dry_run     = program.get<bool>("--dry-run");
    args.verbose     = program.get<bool>("--verbose");

    if (args.dry_run) {
#ifndef NDEVENV
        gucc::utils::default_runner().set_dry_run(true);
#endif
    }
    if (args.verbose) {
        spdlog::set_level(spdlog::level::debug);
    }

    return args;
}

}  // namespace

namespace cli {

auto parse(int argc, char** argv) noexcept -> std::optional<Args> {
    return do_parse(argc, argv);
}

}  // namespace cli
