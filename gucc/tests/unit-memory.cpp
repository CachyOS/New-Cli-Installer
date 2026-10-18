#include "doctest_compatibility.h"

#include "gucc/memory.hpp"

#include <cmath>   // for nan
#include <cstdint> // for uint64_t

#include <limits> // for numeric_limits

namespace memory = gucc::memory;

static_assert(memory::unit_multiplier('K') == memory::kKiB);
static_assert(memory::unit_multiplier('m') == memory::kMiB);
static_assert(memory::unit_multiplier('G') == memory::kGiB);
static_assert(memory::unit_multiplier('t') == memory::kTiB);
static_assert(!memory::unit_multiplier('P'));
static_assert(!memory::unit_multiplier('B'));
static_assert(!memory::unit_multiplier('\0'));

static_assert(memory::scale_bytes(3, memory::kGiB) == 3 * memory::kGiB);
static_assert(memory::scale_bytes(16777215, memory::kTiB) == std::uint64_t{16777215} << 40U);
static_assert(!memory::scale_bytes(16777216, memory::kTiB));
static_assert(memory::scale_bytes(std::numeric_limits<std::uint64_t>::max(), 1) == std::numeric_limits<std::uint64_t>::max());

TEST_CASE("memory")
{
    SECTION("simply scale")
    {
        REQUIRE_EQ(memory::scale_fractional_bytes(1.5, memory::kGiB), memory::kGiB + (memory::kGiB / 2));
        REQUIRE_EQ(memory::scale_fractional_bytes(0.0, memory::kTiB), 0u);
        REQUIRE_EQ(memory::scale_fractional_bytes(16777215.0, memory::kTiB), std::uint64_t{16777215} << 40U);
    }
    SECTION("rejects values without a uint64 representation")
    {
        REQUIRE_FALSE(memory::scale_fractional_bytes(-1.0, 1));
        REQUIRE_FALSE(memory::scale_fractional_bytes(std::nan(""), 1));
        REQUIRE_FALSE(memory::scale_fractional_bytes(std::numeric_limits<double>::infinity(), 1));
        REQUIRE_FALSE(memory::scale_fractional_bytes(16777216.0, memory::kTiB));
    }
    SECTION("running system")
    {
        REQUIRE_GT(memory::total_bytes(), 0u);
    }
}
