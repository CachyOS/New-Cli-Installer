#pragma once

#include "gucc/error.hpp"

#include <string_view>  // for string_view

namespace gucc::firewall {

// Applies the ufw configuration on the target
auto enable_ufw(std::string_view root_mountpoint, bool allow_kdeconnect) noexcept -> Result<void>;

}  // namespace gucc::firewall
