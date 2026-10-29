#pragma once

#include <cstdint>  // for uint64_t

#include <expected>     // for expected
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view

namespace cachyos::installer {

[[nodiscard]] auto validate_hostname(std::string_view hostname) noexcept -> std::expected<void, std::string>;

[[nodiscard]] auto validate_username(std::string_view username) noexcept -> std::expected<void, std::string>;

[[nodiscard]] inline auto is_valid_hostname(std::string_view hostname) noexcept -> bool {
    return validate_hostname(hostname).has_value();
}

[[nodiscard]] inline auto is_valid_username(std::string_view username) noexcept -> bool {
    return validate_username(username).has_value();
}

[[nodiscard]] auto is_valid_zpool_name(std::string_view name) noexcept -> bool;

/// Dataset path relative to its pool (`data/zname`).
[[nodiscard]] auto is_valid_zfs_dataset_name(std::string_view name) noexcept -> bool;

[[nodiscard]] auto is_valid_zfs_property(std::string_view assignment) noexcept -> bool;

/// Positive decimal count.
[[nodiscard]] auto is_valid_zvol_size_mib(std::string_view size) noexcept -> bool;

[[nodiscard]] auto parse_size(std::string_view size) noexcept -> std::optional<std::uint64_t>;

/// Absolute path other than root.
[[nodiscard]] auto is_valid_mountpoint(std::string_view mountpoint) noexcept -> bool;

}  // namespace cachyos::installer
