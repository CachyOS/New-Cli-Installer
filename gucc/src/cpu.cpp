#include "gucc/cpu.hpp"

#include <sys/utsname.h>  // for uname

#include <cstdint>  // for uint32_t, uint64_t
#include <cstring>  // for memcpy

#include <array>        // for array
#include <string_view>  // for string_view

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::cpu {

/* clang-format off */
namespace {

// NOLINTBEGIN
static uint64_t xgetbv() noexcept {
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
static auto is_zen45() noexcept -> bool {
#if defined(__x86_64__) || defined(__i386__)
    if (get_cpu_vendor() != CpuVendor::AMD) {
        spdlog::debug("is_zen45: not an AMD CPU, skipping");
        return false;
    }

    // ref based on PR from GUI installer
    // https://github.com/CachyOS/cachyos-calamares/pull/154

    uint32_t regs[4]{};
    cpuid(regs, 1);

    const uint32_t eax = regs[0];

    // Extract base family/model and extended family/model from CPUID leaf 1
    const uint32_t base_family = (eax >> 8) & 0xF;
    const uint32_t base_model  = (eax >> 4) & 0xF;
    const uint32_t ext_family  = (eax >> 20) & 0xFF;
    const uint32_t ext_model   = (eax >> 16) & 0xF;

    // AMD (base family >= 0x0F): display family = base + extended, display model = (ext << 4) + base
    uint32_t family = base_family;
    uint32_t model  = base_model;
    if (base_family >= 0x0F) {
        family = base_family + ext_family;
        model  = (ext_model << 4) + base_model;
    }

    spdlog::debug("is_zen45: CPUID raw EAX=0x{:08X}, base_family=0x{:X}, base_model=0x{:X}, ext_family=0x{:X}, ext_model=0x{:X}", eax, base_family, base_model, ext_family, ext_model);
    spdlog::debug("is_zen45: display family=0x{:X} ({}), display model=0x{:X} ({})", family, family, model, model);

    switch (family) {
    case 0x19: {  // Family 25: Zen 3 / Zen 4
        if (model <= 0x0F) {
            spdlog::debug("is_zen45: family 0x19 model 0x{:X} is Zen 3, not Zen 4+", model);
            return false;  // Zen 3
        }
        if ((model >= 0x10 && model <= 0x1F)
            || (model >= 0x60 && model <= 0xAF)) {
            spdlog::debug("is_zen45: family 0x19 model 0x{:X} detected as Zen 4", model);
            return true;  // Zen 4
        }
        // Fallback: check for AVX-512 support (covers unknown Zen 4 models)
        const int32_t features = get_cpu_features();
        const bool has_avx512 = (features & AVX512F) != 0;
        spdlog::debug("is_zen45: family 0x19 model 0x{:X} unknown model range, AVX-512 fallback={}", model, has_avx512);
        return has_avx512;
    }
    case 0x1A:  // Family 26: Zen 5+
        spdlog::debug("is_zen45: family 0x1A detected as Zen 5+");
        return true;
    default:
        spdlog::debug("is_zen45: family 0x{:X} is not Zen 4/5", family);
        break;
    }
#endif
    return false;
}

// NOLINTEND

}  // namespace
/* clang-format on */

auto get_isa_levels() noexcept -> std::vector<std::string> {
    std::vector<std::string> supported_isa_levels;

    {
        struct utsname un{};
        uname(&un);
        supported_isa_levels.emplace_back(un.machine);
    }

    const int32_t features = get_cpu_features();

    /* Check for x86_64_v2 */
    const bool supports_v2 = (features & SSSE3) && (features & SSE41) && (features & SSE42);
    if (supports_v2) {
        spdlog::info("cpu supports x86_64_v2");
        supported_isa_levels.emplace_back("x86_64_v2");
    }

    /* Check for x86_64_v3 */
    const bool supports_v3 = (supports_v2) && (features & AVX) && (features & AVX2);
    if (supports_v3) {
        spdlog::info("cpu supports x86_64_v3");
        supported_isa_levels.emplace_back("x86_64_v3");
    }

    /* Check for x86_64_v4 */
    const bool supports_v4 = (supports_v3) && (features & AVX512F) && (features & AVX512VL) && (features & AVX512BW) && (features & AVX512CD) && (features & AVX512DQ);
    if (supports_v4) {
        spdlog::info("cpu supports x86_64_v4");
        supported_isa_levels.emplace_back("x86_64_v4");
    }

    /* Check for znver4/znver5 */
    if (is_zen45()) {
        spdlog::info("cpu is znver4 or znver5");
        supported_isa_levels.emplace_back("znver4");
    }

    return supported_isa_levels;
}

auto get_cpu_vendor() noexcept -> CpuVendor {
#if defined(__x86_64__) || defined(__i386__)
    uint32_t regs[4]{};
    cpuid(regs, 0);

    // fetch vendor string
    std::array<char, 13> vendor{};
    std::memcpy(vendor.data(), &regs[1], 4);      // EBX
    std::memcpy(vendor.data() + 4, &regs[3], 4);  // EDX
    std::memcpy(vendor.data() + 8, &regs[2], 4);  // ECX
    vendor[12] = '\0';

    std::string_view vendor_string = vendor.data();
    if (vendor_string == "AuthenticAMD"sv) {
        return CpuVendor::AMD;
    }
    if (vendor_string == "GenuineIntel"sv) {
        return CpuVendor::Intel;
    }
#endif
    return CpuVendor::Unknown;
}

}  // namespace gucc::cpu
