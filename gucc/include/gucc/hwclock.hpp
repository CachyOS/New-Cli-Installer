#ifndef HWCLOCK_HPP
#define HWCLOCK_HPP

#include <string_view>  // for string_view

namespace gucc::hwclock {

// set utc hwclock
auto set_hwclock_utc(std::string_view root_mountpoint) noexcept -> bool;

// set localtime hwclock
auto set_hwclock_localtime(std::string_view root_mountpoint) noexcept -> bool;

}  // namespace gucc::hwclock

#endif  // HWCLOCK_HPP
