#ifndef AUTOLOGIN_HPP
#define AUTOLOGIN_HPP

#include "gucc/export.hpp"

#include "gucc/error.hpp"

#include <string_view>  // for string_view

GUCC_EXPORT_BEGIN

namespace gucc::user {

// Enable autologin for specified user
auto enable_autologin(std::string_view displaymanager, std::string_view username, std::string_view root_mountpoint) noexcept -> Result<void>;

}  // namespace gucc::user

GUCC_EXPORT_END

#endif  // AUTOLOGIN_HPP
