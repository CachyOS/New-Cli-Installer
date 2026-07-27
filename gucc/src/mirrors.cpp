#include "gucc/mirrors.hpp"
#include "gucc/process.hpp"

#include <spdlog/spdlog.h>

namespace gucc::mirrors {

auto rank_mirrors() noexcept -> Result<void> {
    using utils::default_runner;
    using utils::ProcessKind;
    using utils::RunOptions;

    spdlog::info("Running rate-mirrors...");
    if (!default_runner().run({"/usr/bin/pacman", "-Sy", "--noconfirm", "--needed", "cachyos-rate-mirrors", "rate-mirrors"}, RunOptions{.kind = ProcessKind::Mutate}).ok()) {
        spdlog::warn("Failed to fetch latest cachyos-rate-mirrors. Continuing..");
    }

    if (!default_runner().run({"/usr/bin/cachyos-rate-mirrors"}, RunOptions{.kind = ProcessKind::Mutate}).ok()) {
        return make_error(ErrorCode::SubprocessFailed, "Failed to rank mirrors");
    }
    spdlog::info("Ranked CachyOS mirrors");
    return {};
}

}  // namespace gucc::mirrors
