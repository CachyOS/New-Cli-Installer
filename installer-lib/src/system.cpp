#include "cachyos/system.hpp"

#include <chrono>    // for seconds
#include <expected>  // for unexpected
#include <string>    // for string

#include <spdlog/spdlog.h>

namespace cachyos::installer {

auto detect_system() noexcept -> std::expected<SystemInfo, std::string> {
    return std::unexpected("not yet implemented");
}

auto check_root() noexcept -> bool {
    return false;
}

auto is_connected() noexcept -> bool {
    return false;
}

auto wait_for_connection(const std::chrono::seconds& /*timeout_secs*/) noexcept -> bool {
    return false;
}

}  // namespace cachyos::installer
