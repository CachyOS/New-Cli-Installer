#ifndef PLYMOUTH_HPP
#define PLYMOUTH_HPP

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::plymouth {

/// True when plymouth is present in the target.
[[nodiscard]] auto is_installed(std::string_view root_mountpoint) noexcept -> bool;

/// List the names of installed plymouth themes.
[[nodiscard]] auto list_themes(std::string_view root_mountpoint) noexcept -> std::vector<std::string>;

/// Set the system default boot-splash theme by invoking
/// `plymouth-set-default-theme <theme>` inside the target chroot.
auto set_default_theme(std::string_view theme, std::string_view root_mountpoint) noexcept -> bool;

}  // namespace gucc::plymouth

#endif  // PLYMOUTH_HPP
