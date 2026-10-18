#include "cachyos/disk.hpp"
#include "cachyos/steps.hpp"

// import gucc
#include "gucc/file_utils.hpp"

#include <filesystem>   // for exists
#include <string_view>  // for string_view
#include <utility>      // for move

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_view_literals;

namespace cachyos::installer::steps {

auto cleanup(const InstallContext& ctx) noexcept -> std::vector<std::string> {
    std::vector<std::string> warnings;

    constexpr auto kLogSource = "/tmp/cachyos-install.log"sv;
    if (std::filesystem::exists(kLogSource)) {
        const auto dest = fmt::format(FMT_COMPILE("{}/cachyos-install.log"), ctx.mountpoint);
        if (auto res = gucc::file_utils::copy_file(kLogSource, dest); !res) {
            warnings.push_back(std::move(res.error().context));
        }
    }

    if (auto res = umount_partitions(ctx.mountpoint, ctx.zfs_zpool_names, ctx.swap_device); !res) {
        warnings.push_back(fmt::format(FMT_COMPILE("Final umount: {}"), res.error()));
    }
    return warnings;
}

}  // namespace cachyos::installer::steps
