#ifndef HWCLOCK_HPP
#define HWCLOCK_HPP

#include "gucc/export.hpp"

#include "gucc/error.hpp"

#include <string_view>  // for string_view

GUCC_EXPORT_BEGIN

namespace gucc::hwclock {

// set utc hwclock
auto set_hwclock_utc(std::string_view root_mountpoint) noexcept -> Result<void>;

// set localtime hwclock
auto set_hwclock_localtime(std::string_view root_mountpoint) noexcept -> Result<void>;

}  // namespace gucc::hwclock

GUCC_EXPORT_END

#endif  // HWCLOCK_HPP
