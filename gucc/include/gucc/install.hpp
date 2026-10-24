#pragma once

#include "gucc/error.hpp"
#include "gucc/initcpio.hpp"
#include "gucc/partition_config.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::install {

// A file to copy from the host into the target.
struct FileCopyEntry {
    std::string source;
    // relative to the mountpoint
    std::string destination;
    bool optional{false};
};

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

    // the generated target config starts from
    std::string_view pacman_conf_base{"/etc/pacman.conf"};

    // pacman mirrorlists to copy from the host
    std::string_view mirrorlist_dir{"/etc/pacman.d"};
};

[[nodiscard]] auto install_base(const InstallConfig& config) noexcept -> Result<void>;

}  // namespace gucc::install
