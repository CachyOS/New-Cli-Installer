#pragma once

// import gucc
#include "gucc/bootloader.hpp"

#include <optional>  // for optional
#include <string>    // for string

namespace cachyos::installer {

/// Grub when unset or unrecognised.
[[nodiscard]] inline auto bootloader_from_name(const std::optional<std::string>& name) noexcept
    -> gucc::bootloader::BootloaderType {
    if (!name) {
        return gucc::bootloader::BootloaderType::Grub;
    }
    const auto parsed = gucc::bootloader::bootloader_from_string(*name);
    return parsed.value_or(gucc::bootloader::BootloaderType::Grub);
}

}  // namespace cachyos::installer
