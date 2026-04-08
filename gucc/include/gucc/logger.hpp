#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <memory>       // for shared_ptr
#include <string>       // for string
#include <string_view>  // for string_view

namespace spdlog {
class logger;
}  // namespace spdlog

namespace gucc::logger {

// Set library default logger
void set_logger(std::shared_ptr<spdlog::logger> default_logger) noexcept;

/// Register a secret (e.g. a passphrase) to be scrubbed from every logged line.
/// Empty values are ignored. Secrets are matched as plain substrings and
/// replaced with a fixed mask. Registration is global and thread-safe; call
/// this as soon as a secret is known so it never reaches the file/stdout sinks.
void register_secret(std::string_view secret) noexcept;

/// Drop all registered secrets.
void clear_secrets() noexcept;

/// Return @p line with every registered secret replaced by a mask. When no
/// secrets are registered the input is returned unchanged (fast path).
[[nodiscard]] auto redact(std::string_view line) noexcept -> std::string;

}  // namespace gucc::logger

#endif  // LOGGER_HPP
