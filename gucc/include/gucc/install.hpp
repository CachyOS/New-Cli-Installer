#ifndef INSTALL_HPP
#define INSTALL_HPP

#include "gucc/initcpio.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/subprocess.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for pair
#include <vector>       // for vector

namespace gucc::install {

// A file to copy from host into the target: {source, destination_relative_to_mountpoint}
using FileCopyEntry = std::pair<std::string, std::string>;

struct InstallConfig {
    std::string_view mountpoint;
    std::string_view packages;
    std::string_view keymap;
    initcpio::InitcpioConfig initcpio_config{};
    bool is_zfs{false};
    bool hostcache{true};

    // Additional files to copy from host into the target
    std::vector<FileCopyEntry> host_files_to_copy{};

    // Additional systemd services to enable
    std::vector<std::string> services_to_enable{};
};

[[nodiscard]] auto install_base(const InstallConfig& config, utils::SubProcess& child) noexcept -> bool;

}  // namespace gucc::install

#endif  // INSTALL_HPP
