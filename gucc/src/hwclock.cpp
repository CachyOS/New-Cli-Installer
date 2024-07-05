#include "gucc/hwclock.hpp"
#include "gucc/io_utils.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::hwclock {

auto set_hwclock_utc(std::string_view root_mountpoint) noexcept -> bool {
    // try default
    static constexpr auto hwclock_cmd = "hwclock --systohc --utc"sv;
    if (utils::arch_chroot_checked(hwclock_cmd, root_mountpoint)) {
        spdlog::info("hwclock UTC set on '{}'", root_mountpoint);
        return true;
    }

    // try fallback with direct ISA
    static constexpr auto hwclock_isa_cmd = "hwclock --systohc --utc --directisa"sv;
    if (utils::arch_chroot_checked(hwclock_isa_cmd, root_mountpoint)) {
        spdlog::info("hwclock UTC set using direct ISA on '{}'", root_mountpoint);
        return true;
    }

    return false;
}

auto set_hwclock_localtime(std::string_view root_mountpoint) noexcept -> bool {
    // try default
    static constexpr auto hwclock_cmd = "hwclock --systohc --localtime"sv;
    if (utils::arch_chroot_checked(hwclock_cmd, root_mountpoint)) {
        spdlog::info("hwclock localtime set on '{}'", root_mountpoint);
        return true;
    }

    return false;
}

}  // namespace gucc::hwclock
