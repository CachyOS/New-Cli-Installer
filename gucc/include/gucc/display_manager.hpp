#ifndef DISPLAY_MANAGER_HPP
#define DISPLAY_MANAGER_HPP

#include "gucc/error.hpp"

#include <cstdint>      // for uint8_t
#include <optional>     // for optional
#include <span>         // for span
#include <string>       // for string
#include <string_view>  // for string_view

namespace gucc::display_manager {

/// Display managers recognised by gucc.
enum class Kind : std::uint8_t {
    Gdm,
    Sddm,
    Lightdm,
    Lxdm,
    Ly,
    Plasmalogin,
    CosmicGreeter,
};

/// All known display managers in preference order.
/// Returns the first DM whose unit exists on the target.
[[nodiscard]] auto known_kinds() noexcept -> std::span<const Kind>;

/// Canonical name (matches the systemd unit basename: "sddm" -> sddm.service).
[[nodiscard]] auto to_string(Kind k) noexcept -> std::string_view;

/// Parse a canonical name back into Kind. Returns nullopt for unknown names.
[[nodiscard]] auto from_string(std::string_view name) noexcept -> std::optional<Kind>;

/// Returns the first DM whose systemd unit exists on the target
/// at @p root_mountpoint. nullopt when no known DM unit is present.
[[nodiscard]] auto detect_installed(std::string_view root_mountpoint) noexcept -> std::optional<Kind>;

/// Enables the DM systemd unit on the target.
/// Fails when the unit doesn't exist or systemctl enable fails.
auto enable(Kind kind, std::string_view root_mountpoint) noexcept -> Result<void>;

/// Pick a non-default LightDM greeter by scanning @p xgreeters_dir.
/// When multiple greeters are present the lexicographically first one is
/// returned for determinism. Returns nullopt when the directory does not
/// exist or contains only the default greeter.
[[nodiscard]] auto pick_lightdm_greeter(std::string_view xgreeters_dir) noexcept -> std::optional<std::string>;

/// Configure LightDM to use a non-default greeter when one is installed
/// on the target.
auto configure_lightdm_greeter(std::string_view root_mountpoint) noexcept -> Result<void>;

}  // namespace gucc::display_manager

#endif  // DISPLAY_MANAGER_HPP
