#include "gucc/swap.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/partition.hpp"
#include "gucc/process.hpp"
#include "gucc/string_utils.hpp"

#include <filesystem>    // for permissions
#include <string>        // for string
#include <system_error>  // for error_code

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace gucc::swap {

auto make_swapfile(std::string_view root_mountpoint, std::string_view swap_size) noexcept -> Result<void> {
    const auto& swapfile_path = utils::target_path(root_mountpoint, "/swapfile"sv);

    if (auto res = utils::exec_or_error({"/usr/bin/fallocate"s, "-l"s, std::string{swap_size}, swapfile_path}, "allocate swapfile"sv); !res) {
        return res;
    }

    // NOTE: dry-run skipped fallocate, so there is no file to chmod
    std::error_code err;
    if (!utils::default_runner().dry_run()) {
        std::filesystem::permissions(swapfile_path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace, err);
    }
    if (err) {
        return make_error(ErrorCode::FileIo, FMT_COMPILE("chmod 600 {}: {}"), swapfile_path, err.message());
    }

    // enable swap on file
    if (auto res = utils::exec_or_error({"/usr/bin/mkswap"s, swapfile_path}, "create swapfile"sv); !res) {
        return res;
    }
    return utils::exec_or_error({"/usr/bin/swapon"s, swapfile_path}, "enable swapfile"sv);
}

auto make_swap_partition(const fs::Partition& swap_partition) noexcept -> Result<void> {
    if (auto res = utils::exec_or_error({"/usr/bin/mkswap"s, swap_partition.device}, "make swap partition"sv); !res) {
        return res;
    }
    return utils::exec_or_error({"/usr/bin/swapon"s, swap_partition.device}, "enable swap partition"sv);
}

auto swapoff(std::string_view device) noexcept -> Result<void> {
    return utils::exec_or_error({"/usr/bin/swapoff"s, std::string{device}}, "disable swap"sv);
}

}  // namespace gucc::swap
