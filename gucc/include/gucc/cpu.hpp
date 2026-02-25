#ifndef CPU_HPP
#define CPU_HPP

#include <cinttypes>  // for uint8_t

#include <string>  // for string
#include <vector>  // for vector

namespace gucc::cpu {

enum class CpuVendor : std::uint8_t {
    Unknown,
    Intel,
    AMD,
};

auto get_isa_levels() noexcept -> std::vector<std::string>;
auto get_cpu_vendor() noexcept -> CpuVendor;

}  // namespace gucc::cpu

#endif  // CPU_HPP
