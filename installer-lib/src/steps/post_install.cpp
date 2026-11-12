#include "cachyos/steps.hpp"

// import gucc
#include "gucc/io_utils.hpp"

#include <vector>  // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace cachyos::installer::steps {

auto post_install(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    if (ctx.post_install_script.empty()) {
        return {};
    }
    spdlog::info("Running post-install script: {}", ctx.post_install_script);
    if (!gucc::utils::exec_checked(std::vector{ctx.post_install_script, ctx.mountpoint})) {
        return std::unexpected(fmt::format(FMT_COMPILE("post-install script failed: {}"), ctx.post_install_script));
    }
    return {};
}

}  // namespace cachyos::installer::steps
