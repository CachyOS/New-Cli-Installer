#include "installer_config_adapter.hpp"

#include "cachyos/system.hpp"

// import gucc
#include "gucc/bootloader.hpp"

#include <fmt/format.h>

namespace {

[[nodiscard]] auto bootloader_from_name(const std::optional<std::string>& name) noexcept
    -> gucc::bootloader::BootloaderType {
    if (!name) {
        return gucc::bootloader::BootloaderType::Grub;
    }
    const auto parsed = gucc::bootloader::bootloader_from_string(*name);
    return parsed.value_or(gucc::bootloader::BootloaderType::Grub);
}

}  // namespace

namespace installer {

auto installer_config_to_inputs(const InstallerConfig& cfg) noexcept
    -> std::expected<HeadlessInputs, std::string> {
    HeadlessInputs inputs{};

    const auto sysinfo = cachyos::installer::detect_system();
    if (!sysinfo) {
        return std::unexpected(fmt::format("detect_system failed: {}", sysinfo.error()));
    }
    inputs.ctx.system_mode = sysinfo->system_mode;

    inputs.ctx.device          = cfg.device.value_or("");
    inputs.ctx.filesystem_name = cfg.fs_name.value_or("");
    inputs.ctx.server_mode     = cfg.server_mode;
    inputs.ctx.kernel          = cfg.kernel.value_or("");
    inputs.ctx.desktop         = cfg.desktop.value_or("");
    inputs.ctx.bootloader      = bootloader_from_name(cfg.bootloader);

    if (cfg.xkbmap) {
        inputs.ctx.keymap = *cfg.xkbmap;
    }

    inputs.sys.hostname = cfg.hostname.value_or("");
    inputs.sys.locale   = cfg.locale.value_or("");
    inputs.sys.xkbmap   = cfg.xkbmap.value_or("");
    inputs.sys.keymap   = cfg.xkbmap.value_or("");
    inputs.sys.timezone = cfg.timezone.value_or("");

    inputs.user.username = cfg.user_name.value_or("");
    inputs.user.password = cfg.user_pass.value_or("");
    inputs.user.shell    = cfg.user_shell.value_or("");

    inputs.root_password = cfg.root_pass.value_or("");

    return inputs;
}

}  // namespace installer
