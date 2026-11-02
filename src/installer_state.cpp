#include "installer_state.hpp"

#include <ranges>  // for ranges::*
#include <vector>  // for vector

namespace {

auto make_state(const cachyos::installer::InstallerConfig& config) noexcept -> tui::InstallerState {
    tui::InstallerState st{};
    auto& ctx = st.inputs.ctx;

    ctx.device                 = config.device.value_or("");
    ctx.filesystem_name        = config.fs_name.value_or("");
    ctx.server_mode            = config.server_mode;
    ctx.kernel                 = config.kernel.value_or("");
    ctx.desktop                = config.desktop.value_or("");
    ctx.hostcache              = config.hostcache;
    ctx.encrypt_swap           = config.encrypt_swap;
    ctx.server_profile         = config.server_profile.value_or("");
    ctx.ssh_authorized_keys    = config.ssh_authorized_keys;
    ctx.server_extra_packages  = config.server_extra_packages;
    ctx.server_extra_tcp_ports = config.server_extra_tcp_ports;
    ctx.server_extra_udp_ports = config.server_extra_udp_ports;
    ctx.net_profiles_user_path = config.net_profiles_path.value_or("");
    ctx.install_chwd_profiles  = config.chwd;
    ctx.carry_live_network     = config.carry_network;
    ctx.disable_os_prober      = !config.os_prober;
    ctx.netinstall_groups      = config.netinstall_groups;
    if (config.xkbmap) {
        ctx.keymap = *config.xkbmap;
    }

    auto& sys    = st.inputs.sys;
    sys.hostname = config.hostname.value_or("");
    sys.locale   = config.locale.value_or("");
    sys.xkbmap   = config.xkbmap.value_or("");
    sys.keymap   = config.xkbmap.value_or("");
    sys.timezone = config.timezone.value_or("");

    // the user is only taken as a whole
    if (config.user_name && config.user_pass && config.user_shell) {
        st.inputs.user.username = *config.user_name;
        st.inputs.user.password = *config.user_pass;
        st.inputs.user.shell    = *config.user_shell;
    }
    st.inputs.user.groups    = config.user_groups;
    st.inputs.user.autologin = config.autologin;
    st.inputs.root_password  = config.root_pass.value_or("");

    st.bootloader = config.bootloader.and_then(gucc::bootloader::bootloader_from_string);

    auto& disk                  = st.disk;
    disk.ready_partitions       = config.partitions;
    disk.mount_opts             = config.mount_opts.value_or("");
    disk.use_default_subvolumes = config.use_default_subvolumes;

    // TODO(vnepogodin): shouldn't require manual conversion
    disk.btrfs_subvolumes = config.subvolumes
        | std::ranges::views::transform([](const auto& subvol) { return gucc::fs::BtrfsSubvolume{.subvolume = subvol.subvolume, .mountpoint = subvol.mountpoint}; })
        | std::ranges::to<std::vector<gucc::fs::BtrfsSubvolume>>();

    st.post_install = config.post_install.value_or("");
    st.menus        = config.menus;
    return st;
}

auto storage() noexcept -> tui::InstallerState& {
    static tui::InstallerState st{};
    return st;
}

}  // namespace

namespace tui {

void init_state(const cachyos::installer::InstallerConfig& config) noexcept {
    storage() = make_state(config);
}

auto state() noexcept -> InstallerState& {
    return storage();
}

}  // namespace tui
