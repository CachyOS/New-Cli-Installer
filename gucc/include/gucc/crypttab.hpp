#ifndef CRYPTTAB_HPP
#define CRYPTTAB_HPP

#include "gucc/export.hpp"

#include "gucc/error.hpp"
#include "gucc/partition.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

GUCC_EXPORT_BEGIN

namespace gucc::fs {

// Generate crypttab
auto generate_crypttab(const std::vector<Partition>& partitions, std::string_view root_mountpoint, std::string_view crypttab_opts) noexcept -> Result<void>;

// Generate crypttab into string
auto generate_crypttab_content(const std::vector<Partition>& partitions, std::string_view crypttab_opts) noexcept -> std::string;

}  // namespace gucc::fs

GUCC_EXPORT_END

#endif  // CRYPTTAB_HPP
