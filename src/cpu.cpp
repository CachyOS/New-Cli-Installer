#include "cpu.hpp"

#include <sys/utsname.h>  // for uname

#include <cstdint>

#include <spdlog/spdlog.h>

namespace utils {

/* clang-format off */
namespace {

static uint64_t xgetbv(void) noexcept {
    uint32_t eax{};
    uint32_t edx{};
    __asm__ __volatile__("xgetbv\n"
                         : "=a"(eax), "=d"(edx)
                         : "c"(0));
    return (static_cast<uint64_t>(edx) << 32) | eax;
}

static void cpuid(uint32_t out[4], uint32_t id) noexcept {
    __asm__ __volatile__("cpuid\n"
                         : "=a"(out[0]), "=b"(out[1]), "=c"(out[2]), "=d"(out[3])
                         : "a"(id));
}

static void cpuidex(uint32_t out[4], uint32_t id, uint32_t sid) noexcept {
    __asm__ __volatile__("cpuid\n"
                         : "=a"(out[0]), "=b"(out[1]), "=c"(out[2]), "=d"(out[3])
                         : "a"(id), "c"(sid));
}

enum cpu_feature {
    SSE2     = 1 << 0,
    SSSE3    = 1 << 1,
    SSE41    = 1 << 2,
    SSE42    = 1 << 3,
    AVX      = 1 << 4,
    AVX2     = 1 << 5,
    AVX512F  = 1 << 6,
    AVX512BW = 1 << 7,
    AVX512CD = 1 << 8,
    AVX512DQ = 1 << 9,
    AVX512VL = 1 << 10,
    /* ... */
    UNDEFINED = 1 << 30
};

static int32_t g_cpu_features = UNDEFINED;

static int32_t get_cpu_features() noexcept {
    if (g_cpu_features != UNDEFINED) {
        return g_cpu_features;
    }
#if defined(__x86_64__) || defined(__i386__)
    uint32_t regs[4]{};
    uint32_t *eax{&regs[0]}, *ebx{&regs[1]}, *ecx{&regs[2]}, *edx{&regs[3]};
    static_cast<void>(edx);
    int32_t features = 0;
    cpuid(regs, 0);
    const int32_t max_id = static_cast<int32_t>(*eax);
    cpuid(regs, 1);
#if defined(__amd64__)
    features |= SSE2;
#else
    if (*edx & (1UL << 26))
        features |= SSE2;
#endif
    if (*ecx & (1UL << 0))
        features |= SSSE3;
    if (*ecx & (1UL << 19))
        features |= SSE41;
    if (*ecx & (1UL << 20))
        features |= SSE42;

    if (*ecx & (1UL << 27)) {  // OSXSAVE
        const uint64_t mask = xgetbv();
        if ((mask & 6) == 6) {  // SSE and AVX states
            if (*ecx & (1UL << 28))
                features |= AVX;
            if (max_id >= 7) {
                cpuidex(regs, 7, 0);
                if (*ebx & (1UL << 5))
                    features |= AVX2;
                if ((mask & 224) == 224) {  // Opmask, ZMM_Hi256, Hi16_Zmm
                    if (*ebx & (1UL << 31))
                        features |= AVX512VL;
                    if (*ebx & (1UL << 16))
                        features |= AVX512F;
                    if (*ebx & (1UL << 30))
                        features |= AVX512BW;
                    if (*ebx & (1UL << 28))
                        features |= AVX512CD;
                    if (*ebx & (1UL << 17))
                        features |= AVX512DQ;
                }
            }
        }
    }
    g_cpu_features = features;
    return features;
#else
    /* How to detect NEON? */
    return 0;
#endif
}

}  // namespace
/* clang-format on */

auto get_isa_levels() noexcept -> std::vector<std::string> {
    std::vector<std::string> supported_isa_levels;

    {
        struct utsname un;
        uname(&un);
        supported_isa_levels.push_back(std::string{un.machine});
    }

    const int32_t features = get_cpu_features();

    /* Check for x86_64_v2 */
    const bool supports_v2 = (features & SSSE3) && (features & SSE41) && (features & SSE42);
    if (supports_v2) {
        spdlog::info("cpu supports x86_64_v2");
        supported_isa_levels.push_back("x86_64_v2");
    }

    /* Check for x86_64_v3 */
    const bool supports_v3 = (supports_v2) && (features & AVX) && (features & AVX2);
    if (supports_v3) {
        spdlog::info("cpu supports x86_64_v3");
        supported_isa_levels.push_back("x86_64_v3");
    }

    /* Check for x86_64_v4 */
    const bool supports_v4 = (supports_v3) && (features & AVX512F) && (features & AVX512VL) && (features & AVX512BW) && (features & AVX512CD) && (features & AVX512DQ);
    if (supports_v4) {
        spdlog::info("cpu supports x86_64_v4");
        supported_isa_levels.push_back("x86_64_v4");
    }

    return supported_isa_levels;
}

}  // namespace utils
