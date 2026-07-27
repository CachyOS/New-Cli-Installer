#include "gucc/firewall.hpp"
#include "gucc/io_utils.hpp"

#include <string_view>  // for string_view
#include <vector>       // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::firewall {

auto enable_ufw(std::string_view root_mountpoint, bool allow_kdeconnect) noexcept -> Result<void> {
    std::vector commands{
        "systemctl enable ufw"sv,
        "ufw default deny incoming"sv,
        "ufw default allow outgoing"sv,
        "ufw enable"sv,
    };
    // enable default rule to allow kde connect
    if (allow_kdeconnect) {
        commands.emplace_back(R"(ufw allow "KDE Connect")");
    }

    for (const auto& command : commands) {
        if (!utils::arch_chroot_checked(command, root_mountpoint)) {
            return make_error(ErrorCode::SubprocessFailed, fmt::format(FMT_COMPILE("Failed to configure ufw on {}: {}"), root_mountpoint, command));
        }
    }
    spdlog::info("ufw enabled on {} (kdeconnect={})", root_mountpoint, allow_kdeconnect);
    return {};
}

}  // namespace gucc::firewall
