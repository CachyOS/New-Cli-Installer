#ifndef CPU_HPP
#define CPU_HPP

#include <string>  // for string
#include <vector>  // for vector

namespace utils {

auto get_isa_levels() noexcept -> std::vector<std::string>;

}  // namespace utils

#endif  // CPU_HPP
