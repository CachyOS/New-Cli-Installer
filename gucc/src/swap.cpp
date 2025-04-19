#include "gucc/swap.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/partition.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

namespace gucc::swap {

auto make_swapfile(std::string_view root_mountpoint, std::string_view swap_size) noexcept -> bool {
    const auto& swapfile_path = fmt::format(FMT_COMPILE("{}/swapfile"), root_mountpoint);

    // allocate size for the file
    const auto& alloc_cmd = fmt::format(FMT_COMPILE("fallocate -l {} {} &>>/tmp/cachyos-install.log"), swap_size, swapfile_path);
    if (!utils::exec_checked(alloc_cmd)) {
        spdlog::error("Failed to allocate swapfile: {}", alloc_cmd);
        return false;
    }

    // create swap
    const auto& mkswap_cmd = fmt::format(FMT_COMPILE("chmod 600 {0} && mkswap {0} &>/dev/null"), swapfile_path);
    if (!utils::exec_checked(mkswap_cmd)) {
        spdlog::error("Failed to run mkswap: {}", mkswap_cmd);
        return false;
    }

    // enable swap on file
    const auto& swapon_cmd = fmt::format(FMT_COMPILE("swapon {} &>/dev/null"), swapfile_path);
    if (!utils::exec_checked(swapon_cmd)) {
        spdlog::error("Failed to run swapon on {}", swapfile_path);
        return false;
    }
    return true;
}

auto make_swap_partition(const fs::Partition& swap_partition) noexcept -> bool {
    // create swap
    const auto& mkswap_cmd = fmt::format(FMT_COMPILE("mkswap {} &>/dev/null"), swap_partition.device);
    if (!utils::exec_checked(mkswap_cmd)) {
        spdlog::error("Failed to make swap partition: {}", swap_partition.device);
        return false;
    }

    // whether existing to newly created, activate swap
    const auto& swapon_cmd = fmt::format(FMT_COMPILE("swapon {} &>/dev/null"), swap_partition.device);
    if (!utils::exec_checked(swapon_cmd)) {
        spdlog::error("Failed to swapon partition: {}", swap_partition.device);
        return false;
    }
    return true;
}

}  // namespace gucc::swap
