#include "cachyos/config.hpp"
#include "cachyos/steps.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

namespace cachyos::installer::steps {

auto users(const UserSettings& user,
    std::string_view root_password,
    const InstallContext& ctx) noexcept -> std::vector<std::string> {
    std::vector<std::string> warnings;
    if (auto res = set_root_password(root_password, ctx.mountpoint); !res) {
        warnings.push_back(fmt::format(FMT_COMPILE("set_root_password: {}"), res.error()));
    }
    if (auto res = create_user(user, ctx.mountpoint, ctx.hostcache); !res) {
        warnings.push_back(fmt::format(FMT_COMPILE("create_user: {}"), res.error()));
    }
    return warnings;
}

}  // namespace cachyos::installer::steps
