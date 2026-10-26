#include "cachyos/result.hpp"
#include "cachyos/steps.hpp"

// import gucc
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/server_profiles.hpp"
#include "gucc/string_utils.hpp"

#include <filesystem>    // for permissions
#include <string>        // for string
#include <string_view>   // for string_view
#include <system_error>  // for error_code
#include <vector>        // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace {

namespace fs = std::filesystem;

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
    const auto ssh_dir      = gucc::utils::target_path(mountpoint, fmt::format(FMT_COMPILE("/home/{}/.ssh"), user.username));
    const auto keys_file    = gucc::utils::target_path(ssh_dir, "/authorized_keys"sv);
    const auto keys_content = fmt::format(FMT_COMPILE("{}\n"), gucc::utils::join(keys, '\n'));

    if (auto res = write_creating_dirs(ssh_dir, keys_file, keys_content); !res) {
        return res;
    }

    // only chown needs to be in the chroot
    const std::vector<std::string> chown_argv{
        "/usr/bin/chown"s,
        "-R"s,
        fmt::format(FMT_COMPILE("{0}:{0}"), user.username),
        fmt::format(FMT_COMPILE("/home/{}/.ssh"), user.username)};
    if (auto res = lift(gucc::utils::arch_chroot_or_error(chown_argv, mountpoint), "failed to set ~/.ssh ownership"sv); !res) {
        return res;
    }

    std::error_code ec{};
    fs::permissions(ssh_dir, fs::perms::owner_all, fs::perm_options::replace, ec);
    if (!ec) {
        fs::permissions(keys_file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    }
    if (ec) {
        return std::unexpected(fmt::format(FMT_COMPILE("failed to set ~/.ssh permissions: {}"), ec.message()));
    }

    // drop in the hardening snippet
    const auto& ssh_snippet = gucc::profile::make_sshd_hardening_config();
    const auto& dropin_dir  = gucc::utils::target_path(mountpoint, "/etc/ssh/sshd_config.d"sv);
    const auto& dest_file   = gucc::utils::target_path(dropin_dir, "/99-cachyos-server.conf"sv);
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
