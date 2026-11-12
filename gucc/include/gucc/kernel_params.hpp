#ifndef KERNEL_PARAMS_HPP
#define KERNEL_PARAMS_HPP

#include "gucc/export.hpp"

#include "gucc/partition.hpp"

#include <array>        // for array
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

GUCC_EXPORT_BEGIN

namespace gucc::fs {

// Default kernel params
constexpr std::array kDefaultKernelParams{
    std::string_view{"quiet"},
    std::string_view{"zswap.enabled=0"},
    std::string_view{"nowatchdog"},
};

/// @brief Get kernel params from partitions information.
/// @param partitions The partitions information.
/// @param kernel_params The kernel params separated by space.
/// @param zfs_root_dataset The root dataset of ZFS.
/// @param systemd_hook The initramfs unlocks LUKS with sd-encrypt.
/// @return A vector of strings representing the kernel parameters.
auto get_kernel_params(const std::vector<Partition>& partitions, std::string_view kernel_params, std::optional<std::string> zfs_root_dataset = std::nullopt, bool systemd_hook = false) noexcept -> std::optional<std::vector<std::string>>;

}  // namespace gucc::fs

GUCC_EXPORT_END

#endif  // KERNEL_PARAMS_HPP
