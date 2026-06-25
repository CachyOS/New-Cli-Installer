#ifndef AUTOLOGIN_HPP
#define AUTOLOGIN_HPP

#include "gucc/error.hpp"

#include <string_view>  // for string_view

namespace gucc::user {

// Enable autologin for specified user
auto enable_autologin(std::string_view displaymanager, std::string_view username, std::string_view root_mountpoint) noexcept -> Result<void>;

}  // namespace gucc::user

#endif  // AUTOLOGIN_HPP
