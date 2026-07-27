#include "cachyos/steps.hpp"

// import gucc
#include "gucc/btrfs.hpp"
#include "gucc/error.hpp"

#include <algorithm>  // for any_of
#include <ranges>     // for ranges::*

#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace cachyos::installer::steps {

auto btrfs_snapshot(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    const bool root_is_btrfs = std::ranges::any_of(ctx.partition_schema,
        [](auto&& part) { return (part.mountpoint == "/"sv) && (part.fstype == "btrfs"sv); });
    if (!root_is_btrfs) {
        return {};
    }

    if (auto res = gucc::fs::create_btrfs_installation_snapshot(ctx.mountpoint); !res) {
        spdlog::warn("btrfs_snapshot: {}", gucc::to_string(res.error()));
        return std::unexpected(fmt::format("btrfs_snapshot: {}", gucc::to_string(res.error())));
    }
    return {};
}

}  // namespace cachyos::installer::steps
