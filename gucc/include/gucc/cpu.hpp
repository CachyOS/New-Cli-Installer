#ifndef CPU_HPP
#define CPU_HPP

#include "gucc/export.hpp"

#include <cinttypes>  // for uint8_t

#include <string>  // for string
#include <vector>  // for vector

GUCC_EXPORT_BEGIN

namespace gucc::cpu {

enum class CpuVendor : std::uint8_t {
    Unknown,
    Intel,
    AMD,
};

auto get_isa_levels() noexcept -> std::vector<std::string>;
auto get_cpu_vendor() noexcept -> CpuVendor;

}  // namespace gucc::cpu

GUCC_EXPORT_END

#endif  // CPU_HPP
