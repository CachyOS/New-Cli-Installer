#ifndef TIMEZONE_HPP
#define TIMEZONE_HPP

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::timezone {

// Set system timezone by creating symlink
auto set_timezone(std::string_view timezone, std::string_view mountpoint) noexcept -> bool;

// Get available timezones
auto get_available_timezones() noexcept -> std::vector<std::string>;

// Get available timezone regions (e.g., "Africa", "America", "Europe")
auto get_timezone_regions() noexcept -> std::vector<std::string>;

// Get zones within a region (e.g., for "Europe": "London", "Paris", "Berlin")
auto get_timezone_zones(std::string_view region) noexcept -> std::vector<std::string>;

}  // namespace gucc::timezone

#endif  // TIMEZONE_HPP
