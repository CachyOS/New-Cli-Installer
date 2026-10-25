#pragma once

#include "gucc/error.hpp"

#include <initializer_list>  // for initializer_list
#include <string_view>       // for string_view

namespace gucc::kmod {

/// Loads modules with modprobe.
auto load(std::initializer_list<std::string_view> modules) noexcept -> Result<void>;

/// Unloads modules with modprobe.
auto unload(std::initializer_list<std::string_view> modules) noexcept -> Result<void>;

}  // namespace gucc::kmod
