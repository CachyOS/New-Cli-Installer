#pragma once

#include "gucc/export.hpp"

#include "gucc/error.hpp"

#include <initializer_list>  // for initializer_list
#include <string_view>       // for string_view

GUCC_EXPORT_BEGIN

namespace gucc::kmod {

/// Loads modules with modprobe.
auto load(std::initializer_list<std::string_view> modules) noexcept -> Result<void>;

/// Unloads modules with modprobe.
auto unload(std::initializer_list<std::string_view> modules) noexcept -> Result<void>;

}  // namespace gucc::kmod

GUCC_EXPORT_END
