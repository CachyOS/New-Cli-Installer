#include "gucc/hwclock.hpp"
#include "gucc/io_utils.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

auto set_hwclock(std::string_view mode, std::string_view root_mountpoint) noexcept -> gucc::Result<void> {
    for (const auto extra : {""sv, " --directisa"sv}) {
        if (gucc::utils::arch_chroot_checked(fmt::format(FMT_COMPILE("hwclock --systohc --{}{}"), mode, extra), root_mountpoint)) {
            spdlog::info("hwclock {} set on '{}'{}", mode, root_mountpoint, extra.empty() ? ""sv : " using direct ISA"sv);
            return {};
        }
    }
    return gucc::make_error(gucc::ErrorCode::SubprocessFailed, fmt::format(FMT_COMPILE("hwclock: failed to set {} clock on '{}'"), mode, root_mountpoint));
}

}  // namespace

namespace gucc::hwclock {

auto set_hwclock_utc(std::string_view root_mountpoint) noexcept -> Result<void> {
    return set_hwclock("utc"sv, root_mountpoint);
}

auto set_hwclock_localtime(std::string_view root_mountpoint) noexcept -> Result<void> {
    return set_hwclock("localtime"sv, root_mountpoint);
}

}  // namespace gucc::hwclock
