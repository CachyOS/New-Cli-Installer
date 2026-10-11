#pragma once

#include "gucc/error.hpp"

#include <cstdint>      // for uint8_t
#include <string_view>  // for string_view

namespace gucc::crypto {

enum class LuksVersion : std::uint8_t {
    Luks1,
    Luks2,
};

auto luks_open(LuksVersion version, std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> Result<void>;
auto luks_format(LuksVersion version, std::string_view luks_pass, std::string_view partition, std::string_view additional_flags = {}) noexcept -> Result<void>;
auto luks_add_key(std::string_view dest_file, std::string_view partition, std::string_view additional_flags = {}) noexcept -> Result<void>;
/// Generates @p dest_file if missing, adds it as a key of @p partition and embeds it in the initramfs.
auto luks_setup_keyfile(std::string_view dest_file, std::string_view mountpoint, std::string_view partition, std::string_view additional_flags = {}) noexcept -> Result<void>;

}  // namespace gucc::crypto
