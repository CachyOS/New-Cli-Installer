#include "cachyos/config.hpp"

#include <expected>     // for unexpected
#include <string>       // for string
#include <string_view>  // for string_view

#include <spdlog/spdlog.h>

namespace cachyos::installer {

auto apply_system_settings(const SystemSettings& /*settings*/, std::string_view /*mountpoint*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto create_user(const UserSettings& /*settings*/, std::string_view /*mountpoint*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto set_root_password(std::string_view /*password*/, std::string_view /*mountpoint*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto enable_autologin(std::string_view /*dm*/, std::string_view /*username*/, std::string_view /*mountpoint*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

}  // namespace cachyos::installer
