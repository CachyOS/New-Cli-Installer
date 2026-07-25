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
void register_secret(std::string_view secret) noexcept;

/// Drop all registered secrets.
void clear_secrets() noexcept;

/// Whether any secret is registered.
[[nodiscard]] auto has_secrets() noexcept -> bool;

/// Return @p line with every registered secret redacted.
[[nodiscard]] auto redact(std::string_view line) noexcept -> std::string;

}  // namespace gucc::logger

#endif  // LOGGER_HPP
