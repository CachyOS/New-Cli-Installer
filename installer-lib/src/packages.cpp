#include "cachyos/packages.hpp"

#include <expected>     // for unexpected
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <spdlog/spdlog.h>

namespace cachyos::installer {

auto install_base(const InstallContext& /*ctx*/, const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto install_desktop(std::string_view /*desktop*/, const InstallContext& /*ctx*/,
    const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto install_packages(const std::vector<std::string>& /*packages*/,
    std::string_view /*mountpoint*/, bool /*hostcache*/,
    const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto remove_packages(const std::vector<std::string>& /*packages*/,
    std::string_view /*mountpoint*/,
    const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto enable_services(const InstallContext& /*ctx*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

auto install_needed(std::string_view /*pkg*/,
    const ExecutionCallbacks& /*callbacks*/) noexcept
    -> std::expected<void, std::string> {
    return std::unexpected("not yet implemented");
}

}  // namespace cachyos::installer
