#ifndef MACHINE_ID_HPP
#define MACHINE_ID_HPP

#include "gucc/error.hpp"

#include <string_view>  // for string_view

namespace gucc::machine_id {

/// Initialize the target system's /etc/machine-id at install time.
/// Returns an error on any filesystem or subprocess failure.
auto reset(std::string_view root_mountpoint) noexcept -> Result<void>;

}  // namespace gucc::machine_id

namespace gucc::machine_id::detail {

/// Remove the inherited machine-id from /etc and the legacy dbus path.
auto clear_existing(std::string_view root_mountpoint) noexcept -> Result<void>;

/// Create the legacy dbus machine-id path as a symlink to /etc/machine-id.
/// Replaces any existing file or stale symlink at that path.
auto link_dbus_to_etc(std::string_view root_mountpoint) noexcept -> Result<void>;

}  // namespace gucc::machine_id::detail

#endif  // MACHINE_ID_HPP
