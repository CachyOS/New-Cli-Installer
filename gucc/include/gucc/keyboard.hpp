#ifndef KEYBOARD_HPP
#define KEYBOARD_HPP

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view

namespace gucc::keyboard {

/// Full keyboard configuration applied to /etc/vconsole.conf and
/// /etc/X11/xorg.conf.d/00-keyboard.conf.
///
/// console_keymap and x11_layout gate whether the respective file is
/// written: std::nullopt skips that target. The remaining fields, when
/// present, emit one line each in the generated file; when absent, the
/// line is omitted so the result stays minimal.
struct Config {
    /// vconsole.conf KEYMAP=
    std::optional<std::string> console_keymap;
    /// vconsole.conf FONT=
    std::optional<std::string> console_font;
    /// xorg XkbLayout
    std::optional<std::string> x11_layout;
    /// xorg XkbModel
    std::optional<std::string> x11_model;
    /// xorg XkbVariant
    std::optional<std::string> x11_variant;
    /// xorg XkbOptions (comma-separated)
    std::optional<std::string> x11_options;
};

/// Generate the body of /etc/vconsole.conf. The FONT line is emitted only
/// when @p font holds a value.
[[nodiscard]] auto make_vconsole_conf(std::string_view keymap,
    std::optional<std::string_view> font = std::nullopt) noexcept -> std::string;

/// Generate the body of /etc/X11/xorg.conf.d/00-keyboard.conf for the
/// given XKB settings. Model, variant and options lines are emitted only
/// when the corresponding argument holds a value.
[[nodiscard]] auto make_x11_keymap_conf(std::string_view layout,
    std::optional<std::string_view> model   = std::nullopt,
    std::optional<std::string_view> variant = std::nullopt,
    std::optional<std::string_view> options = std::nullopt) noexcept -> std::string;

/// Apply the keyboard configuration to the target system rooted at
/// @p root_mountpoint.
/// A config with both unset succeeds without touching the target.
/// Returns false on any filesystem error.
auto apply(const Config& cfg, std::string_view root_mountpoint) noexcept -> bool;

}  // namespace gucc::keyboard

#endif  // KEYBOARD_HPP
