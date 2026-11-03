#pragma once

#include "gucc/export.hpp"

#include "gucc/error.hpp"

GUCC_EXPORT_BEGIN

namespace gucc::mirrors {

auto rank_mirrors() noexcept -> Result<void>;

}  // namespace gucc::mirrors

GUCC_EXPORT_END
