#ifndef CHWD_HPP
#define CHWD_HPP

#include "gucc/error.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::chwd {

// Retrieves all profiles from chwd(e.g. chwd --list-all)
auto get_all_profile_names() noexcept -> std::vector<std::string>;

// Retrieves all possible profiles for the hardware from chwd(e.g. chwd --list)
auto get_available_profile_names() noexcept -> std::vector<std::string>;

// Installs possible profiles for the hardware using chwd -a
auto install_available_profiles(std::string_view root_mountpoint) noexcept -> Result<void>;

}  // namespace gucc::chwd

namespace gucc::chwd::detail {

// every row of list all
auto parse_profile_table(std::string_view output) noexcept -> std::vector<std::string>;

// value of every `Name` row
auto parse_profile_details(std::string_view output) noexcept -> std::vector<std::string>;

}  // namespace gucc::chwd::detail

#endif  // CHWD_HPP
