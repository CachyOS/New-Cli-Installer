#include "gucc/logger.hpp"

#include <utility>  // for move

#include <spdlog/spdlog-inl.h>

namespace gucc::logger {

void set_logger(std::shared_ptr<spdlog::logger> default_logger) noexcept {
    spdlog::set_default_logger(std::move(default_logger));
}

}  // namespace gucc::logger
