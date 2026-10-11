#pragma once

#include "gucc/error.hpp"

#include <string_view>  // for string_view

namespace gucc::crypto {

// LUKS version used in cryptsetup commands.
enum class LUKSVersion : uint8_t { V1, V2 };

// Open a LUKS partition with the given password.
[[nodiscard]] auto luks_open(LUKSVersion version, std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> Result<void>;
// Format (initialize) a LUKS partition with the given password.
[[nodiscard]] auto luks_format(LUKSVersion version, std::string_view luks_pass, std::string_view partition, std::string_view additional_flags = {}) noexcept -> Result<void>;
// Add a keyfile to an existing LUKS partition.
[[nodiscard]] auto luks_add_key(LUKSVersion version, std::string_view dest_file, std::string_view partition, std::string_view additional_flags = {}) noexcept -> Result<void>;
// Set up a keyfile for LUKS (generate, add to crypttab, update initramfs).
[[nodiscard]] auto luks_setup_keyfile(LUKSVersion version, std::string_view dest_file, std::string_view mountpoint, std::string_view partition, std::string_view additional_flags = {}) noexcept -> Result<void>;

// Convenience wrappers — kept for backward compatibility.
[[nodiscard]] auto luks1_open(std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> Result<void>;
[[nodiscard]] auto luks1_format(std::string_view luks_pass, std::string_view partition, std::string_view additional_flags = {}) noexcept -> Result<void>;
[[nodiscard]] auto luks1_add_key(std::string_view dest_file, std::string_view partition, std::string_view additional_flags = {}) noexcept -> Result<void>;
[[nodiscard]] auto luks1_setup_keyfile(std::string_view dest_file, std::string_view mountpoint, std::string_view partition, std::string_view additional_flags = {}) noexcept -> Result<void>;

[[nodiscard]] auto luks2_open(std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> Result<void>;
[[nodiscard]] auto luks2_format(std::string_view luks_pass, std::string_view partition, std::string_view additional_flags = {}) noexcept -> Result<void>;
[[nodiscard]] auto luks2_add_key(std::string_view dest_file, std::string_view partition, std::string_view additional_flags = {}) noexcept -> Result<void>;
[[nodiscard]] auto luks2_setup_keyfile(std::string_view dest_file, std::string_view mountpoint, std::string_view partition, std::string_view additional_flags = {}) noexcept -> Result<void>;

}  // namespace gucc::crypto
