#ifndef CACHYOS_INSTALLER_PACKAGES_HPP
#define CACHYOS_INSTALLER_PACKAGES_HPP

#include "cachyos/types.hpp"

#include <expected>     // for expected
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

// forward-declare
namespace gucc::utils {
class SubProcess;
}

namespace cachyos::installer {

/// Installs the base system packages.
[[nodiscard]] auto install_base(const InstallContext& ctx, gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string>;

/// Installs the selected desktop environment.
[[nodiscard]] auto install_desktop(std::string_view desktop, const InstallContext& ctx,
    gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string>;

/// Installs an arbitrary set of packages into the target system.
[[nodiscard]] auto install_packages(const std::vector<std::string>& packages,
    std::string_view mountpoint, bool hostcache,
    gucc::utils::SubProcess& child) noexcept
    -> std::expected<void, std::string>;

/// Removes packages from the target system.
[[nodiscard]] auto remove_packages(const std::vector<std::string>& packages,
    std::string_view mountpoint,
    const ExecutionCallbacks& callbacks) noexcept
    -> std::expected<void, std::string>;

/// Enables systemd/openrc services on the installed system.
[[nodiscard]] auto enable_services(const InstallContext& ctx) noexcept
    -> std::expected<void, std::string>;

/// Ensures a package is installed on the live system (not the target).
[[nodiscard]] auto install_needed(std::string_view pkg,
    const ExecutionCallbacks& callbacks) noexcept
    -> std::expected<void, std::string>;

}  // namespace cachyos::installer

#endif  // CACHYOS_INSTALLER_PACKAGES_HPP
