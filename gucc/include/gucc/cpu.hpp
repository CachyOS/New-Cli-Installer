#ifndef CPU_HPP
#define CPU_HPP

#include <string>  // for string
#include <vector>  // for vector

namespace gucc::cpu {

auto get_isa_levels() noexcept -> std::vector<std::string>;

}  // namespace gucc::cpu

#endif  // CPU_HPP
