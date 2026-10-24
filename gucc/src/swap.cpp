#include "gucc/swap.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/partition.hpp"
#include "gucc/string_utils.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_view_literals;

namespace gucc::swap {

auto make_swapfile(std::string_view root_mountpoint, std::string_view swap_size) noexcept -> Result<void> {
    const auto& swapfile_path = utils::target_path(root_mountpoint, "/swapfile"sv);

    // allocate size for the file
    const auto& alloc_cmd = fmt::format(FMT_COMPILE("fallocate -l {} {}"), swap_size, swapfile_path);
    if (auto res = utils::exec_or_error(alloc_cmd, "allocate swapfile"sv); !res) {
        return res;
    }

    // create swap
    const auto& mkswap_cmd = fmt::format(FMT_COMPILE("chmod 600 {0} && mkswap {0}"), swapfile_path);
    if (auto res = utils::exec_or_error(mkswap_cmd, "create swapfile"sv); !res) {
        return res;
    }

    // enable swap on file
    const auto& swapon_cmd = fmt::format(FMT_COMPILE("swapon {}"), swapfile_path);
    return utils::exec_or_error(swapon_cmd, "enable swapfile"sv);
}

auto make_swap_partition(const fs::Partition& swap_partition) noexcept -> Result<void> {
    // create swap
    const auto& mkswap_cmd = fmt::format(FMT_COMPILE("mkswap {}"), swap_partition.device);
    if (auto res = utils::exec_or_error(mkswap_cmd, "make swap partition"sv); !res) {
        return res;
    }

    // whether existing to newly created, activate swap
    const auto& swapon_cmd = fmt::format(FMT_COMPILE("swapon {}"), swap_partition.device);
    return utils::exec_or_error(swapon_cmd, "enable swap partition"sv);
}

}  // namespace gucc::swap
