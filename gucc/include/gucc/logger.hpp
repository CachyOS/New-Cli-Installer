#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <memory>  // for shared_ptr

#include <spdlog/spdlog-inl.h>

namespace gucc::logger {

// Set library default logger
void set_logger(std::shared_ptr<spdlog::logger> default_logger) noexcept;

}  // namespace gucc::logger

#endif  // LOGGER_HPP
