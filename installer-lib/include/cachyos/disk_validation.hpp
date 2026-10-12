#pragma once

#include "cachyos/installer_config.hpp"

#include <cstdint>    // for uint64_t
#include <expected>   // for expected
#include <optional>   // for optional
#include <string_view>// for string_view
#include <vector>     // for vector

namespace cachyos::installer {

// Parse size strings like "512M", "4G", "100%" into bytes. Returns nullopt for "100%".
[[nodiscard]] auto parse_size_bytes(std::string_view) noexcept -> std::optional<uint64_t>;

// Recommend swap size based on installed RAM (bytes).
[[nodiscard]] auto recommend_swap_size(uint64_t ram_bytes) noexcept -> uint64_t;

// Validate disk space before installation. Returns error string if validation fails, empty expected otherwise.
[[nodiscard]] auto validate_disk_space(const InstallerConfig& config, bool is_efi) noexcept -> std::expected<void, std::string>;

}  // namespace cachyos::installer
