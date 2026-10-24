#ifndef CLI_HPP
#define CLI_HPP

#include <string>
#include <optional>

namespace cli {

/// Parsed results from command-line argument parsing.
struct Args {
    std::string config_path;
    bool dry_run    = false;
    bool verbose    = false;
};

/// Parse argc/argv and return the parsed arguments.
/// May return std::nullopt on --help or on parse error.
/// Sets spdlog level to debug when --verbose is present.
auto parse(int argc, char** argv) noexcept
    -> std::optional<Args>;

}  // namespace cli

#endif  // CLI_HPP
