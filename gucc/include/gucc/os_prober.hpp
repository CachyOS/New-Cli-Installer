#pragma once

#include "gucc/export.hpp"

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

GUCC_EXPORT_BEGIN

namespace gucc::os_prober {

struct Entry final {
    std::string device;
    std::optional<std::string> efi_path;
    std::string pretty_name;
};

/// @brief Runs host os-prober.
/// @return Empty when it is missing, fails or finds nothing.
auto probe() noexcept -> std::vector<Entry>;

}  // namespace gucc::os_prober

namespace gucc::os_prober::detail {

auto parse(std::string_view output) noexcept -> std::vector<Entry>;

}  // namespace gucc::os_prober::detail

GUCC_EXPORT_END
