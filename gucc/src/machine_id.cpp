#include "gucc/machine_id.hpp"
#include "gucc/io_utils.hpp"

#include <filesystem>  // for path, remove
#include <system_error>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace gucc::machine_id::detail {

auto clear_existing(std::string_view root_mountpoint) noexcept -> bool {
    const fs::path root{root_mountpoint};
    std::error_code ec;
    fs::remove(root / "etc" / "machine-id", ec);
    if (ec) {
        spdlog::error("machine_id: failed to remove /etc/machine-id under '{}': {}", root_mountpoint, ec.message());
        return false;
    }
    fs::remove(root / "var" / "lib" / "dbus" / "machine-id", ec);
    if (ec) {
        spdlog::error("machine_id: failed to remove /var/lib/dbus/machine-id under '{}': {}", root_mountpoint, ec.message());
        return false;
    }
    return true;
}

auto link_dbus_to_etc(std::string_view root_mountpoint) noexcept -> bool {
    const fs::path root{root_mountpoint};
    const auto dbus_path = root / "var" / "lib" / "dbus" / "machine-id";

    std::error_code ec;
    fs::create_directories(dbus_path.parent_path(), ec);
    if (ec) {
        spdlog::error("machine_id: failed to create {}: {}", dbus_path.parent_path().string(), ec.message());
        return false;
    }
    // Replace any leftover regular file or stale symlink.
    fs::remove(dbus_path, ec);
    if (ec) {
        spdlog::error("machine_id: failed to clear {}: {}", dbus_path.string(), ec.message());
        return false;
    }
    fs::create_symlink("/etc/machine-id", dbus_path, ec);
    if (ec) {
        spdlog::error("machine_id: failed to symlink {} -> /etc/machine-id: {}", dbus_path.string(), ec.message());
        return false;
    }
    return true;
}

}  // namespace gucc::machine_id::detail

namespace gucc::machine_id {

auto reset(std::string_view root_mountpoint) noexcept -> bool {
    if (!detail::clear_existing(root_mountpoint)) {
        return false;
    }

    // TODO(vnepogodin): expose a wrapper around systemd-firstboot
    // to seed several first-boot settings in single call.

    const auto cmd = fmt::format("systemd-machine-id-setup --root={}", root_mountpoint);
    if (!utils::exec_checked(cmd)) {
        spdlog::error("machine_id: systemd-machine-id-setup failed for root '{}'", root_mountpoint);
        return false;
    }

    return detail::link_dbus_to_etc(root_mountpoint);
}

}  // namespace gucc::machine_id
