#ifndef AUTOLOGIN_HPP
#define AUTOLOGIN_HPP

#include <string_view>  // for string_view

namespace gucc::user {

// Enables autologin for user
auto enable_autologin(std::string_view displaymanager, std::string_view username, std::string_view root_mountpoint) noexcept -> bool;

}  // namespace gucc::user

#endif  // AUTOLOGIN_HPP
