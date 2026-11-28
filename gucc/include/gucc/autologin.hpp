#pragma once

#include "gucc/export.hpp"

#include "gucc/error.hpp"

#include <optional>     // for optional
#include <string_view>  // for string_view

GUCC_EXPORT_BEGIN

namespace gucc::user {

// Enable autologin for specified user
auto enable_autologin(std::string_view displaymanager, std::string_view username,
    std::string_view root_mountpoint, std::optional<std::string_view> desktop = std::nullopt) noexcept
    -> Result<void>;

}  // namespace gucc::user

GUCC_EXPORT_END
