#include "gucc/mirrors.hpp"
#include "gucc/io_utils.hpp"

#include <string>  // for string
#include <vector>  // for vector

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::mirrors {

auto rank_mirrors() noexcept -> Result<void> {
    spdlog::info("Running rate-mirrors...");
    if (!utils::exec_checked({"/usr/bin/pacman", "-Sy", "--noconfirm", "--needed", "cachyos-rate-mirrors", "rate-mirrors"})) {
        spdlog::warn("Failed to fetch latest cachyos-rate-mirrors. Continuing..");
    }

    if (auto res = utils::exec_or_error(std::vector<std::string>{"/usr/bin/cachyos-rate-mirrors"}, "rank mirrors"sv); !res) {
        return res;
    }
    spdlog::info("Ranked CachyOS mirrors");
    return {};
}

}  // namespace gucc::mirrors
