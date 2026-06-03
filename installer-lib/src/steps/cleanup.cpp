#include "cachyos/disk.hpp"
#include "cachyos/steps.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <string_view>
#include <system_error>

using namespace std::string_view_literals;

namespace fs = std::filesystem;

namespace cachyos::installer::steps {

auto cleanup(const InstallContext& ctx) noexcept -> std::vector<std::string> {
    std::vector<std::string> warnings;

    constexpr auto kLogSource = "/tmp/cachyos-install.log"sv;
    if (fs::exists(kLogSource)) {
        const auto dest = ctx.mountpoint + "/cachyos-install.log";
        std::error_code ec;
        fs::copy_file(kLogSource, dest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            spdlog::warn("copy install log: {}", ec.message());
        }
    }

    if (auto res = umount_partitions(ctx.mountpoint, ctx.zfs_zpool_names, ctx.swap_device); !res) {
        spdlog::warn("Final umount: {}", res.error());
        warnings.emplace_back(fmt::format("Final umount: {}", res.error()));
    }
    return warnings;
}

}  // namespace cachyos::installer::steps
