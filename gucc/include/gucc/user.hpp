#ifndef USER_HPP
#define USER_HPP

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::user {

struct UserInfo final {
    std::string_view username;
    std::string_view password;
    std::string_view shell;
    std::string_view sudoers_group;
};

// Create group on the system
auto create_group(std::string_view group, std::string_view mountpoint) noexcept -> bool;

// Create user on the system
auto create_new_user(const user::UserInfo& user_info, const std::vector<std::string>& default_groups, std::string_view mountpoint) noexcept -> bool;

}  // namespace gucc::user

#endif  // USER_HPP
