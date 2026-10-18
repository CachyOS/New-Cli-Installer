#include "gucc/memory.hpp"

#include <sys/sysinfo.h>  // for sysinfo

#include <limits>  // for numeric_limits

namespace gucc::memory {

auto total_bytes() noexcept -> std::uint64_t {
    struct ::sysinfo info{};
    if (::sysinfo(&info) != 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(info.totalram) * info.mem_unit;
}

constexpr auto kUint64Limit = static_cast<double>(std::numeric_limits<std::uint64_t>::max());
static_assert(kUint64Limit == 0x1p64);

auto scale_fractional_bytes(double value, std::uint64_t multiplier) noexcept -> std::optional<std::uint64_t> {
    const auto bytes = value * static_cast<double>(multiplier);
    // nan should also be rejected input
    if (!(bytes >= 0.0) || bytes >= kUint64Limit) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(bytes);
}

}  // namespace gucc::memory
