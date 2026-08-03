#include "cachyos/steps.hpp"

// import gucc
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/server_profiles.hpp"
#include "gucc/string_utils.hpp"

#include <filesystem>  // for create_directories
#include <string>      // for string

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace cachyos::installer::steps {

auto ssh_keys(const UserSettings& user, const InstallContext& ctx) noexcept
    -> std::expected<void, std::string> {
    if (!ctx.resolved_server) {
        return {};
    }
    const auto& keys = ctx.resolved_server->ssh_authorized_keys;
    if (keys.empty()) {
        return std::unexpected("ssh_keys: server profile resolved with no authorized keys");
    }

    const auto& mountpoint = ctx.mountpoint;

    // write ~/.ssh/authorized_keys
    const auto ssh_dir      = fmt::format(FMT_COMPILE("{}/home/{}/.ssh"), mountpoint, user.username);
    const auto keys_file    = fmt::format(FMT_COMPILE("{}/authorized_keys"), ssh_dir);
    const auto keys_content = fmt::format(FMT_COMPILE("{}\n"), gucc::utils::join(keys, '\n'));

    std::error_code ec{};
    fs::create_directories(ssh_dir, ec);
    if (ec) {
        return std::unexpected(fmt::format("ssh_keys: cannot create '{}': {}", ssh_dir, ec.message()));
    }
    if (!gucc::file_utils::create_file_for_overwrite(keys_file, keys_content)) {
        return std::unexpected(fmt::format("ssh_keys: cannot write '{}'", keys_file));
    }

    // set correct perms on ssh dir
    const auto fixup = fmt::format(FMT_COMPILE(
                                       "chown -R {0}:{0} /home/{0}/.ssh && chmod 700 /home/{0}/.ssh && chmod 600 /home/{0}/.ssh/authorized_keys"),
        user.username);
    if (!gucc::utils::arch_chroot_checked(fixup, mountpoint)) {
        return std::unexpected("ssh_keys: failed to set ~/.ssh ownership/permissions");
    }

    // drop in the hardening snippet
    const auto dropin_path = fmt::format(FMT_COMPILE("{}/etc/ssh/sshd_config.d/99-cachyos-server.conf"), mountpoint);
    std::error_code dropin_ec{};
    fs::create_directories(fmt::format(FMT_COMPILE("{}/etc/ssh/sshd_config.d"), mountpoint), dropin_ec);
    if (!gucc::file_utils::create_file_for_overwrite(dropin_path, gucc::profile::make_sshd_hardening_config())) {
        return std::unexpected("ssh_keys: failed to write sshd hardening drop-in");
    }

    // fresh host keys
    if (!gucc::utils::arch_chroot_checked("ssh-keygen -A", mountpoint)) {
        spdlog::warn("ssh_keys: failed to generate host keys");
    }

    // sanity check
    if (!gucc::utils::arch_chroot_checked("sshd -t", mountpoint)) {
        spdlog::warn("ssh_keys: 'sshd -t' reported a problem with the sshd configuration");
    }

    return {};
}

}  // namespace cachyos::installer::steps
