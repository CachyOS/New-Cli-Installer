#pragma once

#include "gucc/export.hpp"

#include "gucc/error.hpp"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint8_t

#include <string_view>  // for string_view

GUCC_EXPORT_BEGIN

namespace gucc::crypto {

enum class LuksVersion : std::uint8_t {
    Luks1,
    Luks2,
};

/// Unlock LUKS partition.
auto luks_open(LuksVersion version, std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> Result<void>;

/// Closes mapper.
auto luks_close(std::string_view luks_name) noexcept -> Result<void>;

/// Format with particular LUKS version.
auto luks_format(LuksVersion version, std::string_view luks_pass, std::string_view partition, std::string_view additional_flags = {}) noexcept -> Result<void>;

/// Add random encr keyfile.
auto luks_add_key(std::string_view dest_file, std::string_view partition, std::string_view additional_flags = {}) noexcept -> Result<void>;

/// Generates random keyfile, and embed into generated initramfs.
auto luks_embed_keyfile(std::string_view dest_file, std::string_view mountpoint, std::string_view partition, std::string_view passphrase, std::string_view additional_flags = {}) noexcept -> Result<void>;

auto luks_setup_keyfile(std::string_view dest_file, std::string_view mountpoint, std::string_view partition, std::string_view additional_flags = {}) noexcept -> Result<void>;

/// Enabled keyslots of @p partition.
auto luks_keyslot_count(std::string_view partition) noexcept -> Result<std::size_t>;

}  // namespace gucc::crypto

namespace gucc::crypto::detail {

[[nodiscard]] auto count_enabled_luks_keyslots(std::string_view luks_dump) noexcept -> std::size_t;

}  // namespace gucc::crypto::detail

GUCC_EXPORT_END
