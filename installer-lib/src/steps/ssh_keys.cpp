#include "cachyos/result.hpp"
#include "cachyos/steps.hpp"

// import gucc
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/server_profiles.hpp"
#include "gucc/string_utils.hpp"

#include <string>       // for string
#include <string_view>  // for string_view

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace {

auto write_creating_dirs(std::string_view dir, std::string_view file, std::string_view content) noexcept
    -> std::expected<void, std::string> {
    return cachyos::installer::lift(gucc::file_utils::create_directories(dir).and_then([&] {
        return gucc::file_utils::write_file(file, content);
    }));
}

}  // namespace

namespace cachyos::installer::steps {

auto ssh_keys(const UserSettings& user, const InstallContext& ctx) noexcept
    -> std::expected<void, std::string> {
    if (!ctx.resolved_server) {
        return {};
    }
    const auto& keys = ctx.resolved_server->ssh_authorized_keys;
    if (keys.empty()) {
        return std::unexpected("server profile resolved with no authorized keys");
    }

    const auto& mountpoint = ctx.mountpoint;

    // write ~/.ssh/authorized_keys
    const auto ssh_dir      = fmt::format(FMT_COMPILE("{}/home/{}/.ssh"), mountpoint, user.username);
    const auto keys_file    = fmt::format(FMT_COMPILE("{}/authorized_keys"), ssh_dir);
    const auto keys_content = fmt::format(FMT_COMPILE("{}\n"), gucc::utils::join(keys, '\n'));

    if (auto res = write_creating_dirs(ssh_dir, keys_file, keys_content); !res) {
        return res;
    }

    // set correct perms on ssh dir
    const auto fixup = fmt::format(FMT_COMPILE(
                                       "chown -R {0}:{0} /home/{0}/.ssh && chmod 700 /home/{0}/.ssh && chmod 600 /home/{0}/.ssh/authorized_keys"),
        user.username);
    if (!gucc::utils::arch_chroot_checked(fixup, mountpoint)) {
        return std::unexpected("failed to set ~/.ssh ownership/permissions");
    }

    // drop in the hardening snippet
    const auto& ssh_snippet = gucc::profile::make_sshd_hardening_config();
    const auto& dropin_dir  = fmt::format(FMT_COMPILE("{}/etc/ssh/sshd_config.d"), mountpoint);
    const auto& dest_file   = fmt::format(FMT_COMPILE("{}/99-cachyos-server.conf"), dropin_dir);
    if (auto res = write_creating_dirs(dropin_dir, dest_file, ssh_snippet); !res) {
        return res;
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
