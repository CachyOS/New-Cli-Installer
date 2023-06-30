#ifndef CHWD_PROFILES_HPP
#define CHWD_PROFILES_HPP

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace detail::chwd {

auto get_all_profile_names(std::string_view profile_type) noexcept -> std::optional<std::vector<std::string>>;
auto get_available_profile_names(std::string_view profile_type) noexcept -> std::optional<std::vector<std::string>>;

}  // namespace detail::chwd

#endif  // CHWD_PROFILES_HPP
