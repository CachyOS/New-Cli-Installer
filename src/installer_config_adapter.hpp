#ifndef INSTALLER_CONFIG_ADAPTER_HPP
#define INSTALLER_CONFIG_ADAPTER_HPP

#include "installer_config.hpp"

#include "cachyos/types.hpp"

#include <expected>
#include <string>

namespace installer {

struct HeadlessInputs {
    cachyos::installer::InstallContext ctx;
    cachyos::installer::SystemSettings sys;
    cachyos::installer::UserSettings user;
    std::string root_password;
};

[[nodiscard]] auto installer_config_to_inputs(const InstallerConfig& cfg) noexcept
    -> std::expected<HeadlessInputs, std::string>;

}  // namespace installer

#endif  // INSTALLER_CONFIG_ADAPTER_HPP
