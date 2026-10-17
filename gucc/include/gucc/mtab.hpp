#pragma once

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::mtab {

struct MTabEntry {
    std::string device{};
    std::string mountpoint{};
    std::string fstype{};
    std::string options{};
};

// Parse mtab
auto parse_mtab(std::string_view root_mountpoint, std::string_view mtab_path = "/etc/mtab") noexcept -> std::optional<std::vector<MTabEntry>>;

// Parse mtab content
auto parse_mtab_content(std::string_view mtab_content, std::string_view root_mountpoint) noexcept -> std::vector<MTabEntry>;

/// Find the last match.
auto find_mount(const std::vector<MTabEntry>& entries, std::string_view mountpoint) noexcept -> std::optional<MTabEntry>;

/// Value of the first key value pair.
auto mount_option_value(std::string_view options, std::string_view key) noexcept -> std::optional<std::string_view>;

}  // namespace gucc::mtab
