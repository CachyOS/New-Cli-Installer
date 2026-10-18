#include "cachyos/result.hpp"
#include "cachyos/steps.hpp"

// import gucc
#include "gucc/btrfs.hpp"

#include <algorithm>    // for any_of
#include <functional>   // for bind_back
#include <string_view>  // for string_view

using namespace std::string_view_literals;

namespace cachyos::installer::steps {

auto btrfs_snapshot(const InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    const bool root_is_btrfs = std::ranges::any_of(ctx.partition_schema, std::bind_back(gucc::fs::is_root_on, "btrfs"sv));
    if (!root_is_btrfs) {
        return {};
    }
    return lift(gucc::fs::create_btrfs_installation_snapshot(ctx.mountpoint));
}

}  // namespace cachyos::installer::steps
