#include "gucc/user.hpp"
#include "gucc/autologin.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>   // for find, contains
#include <filesystem>  // for permissions
#include <ranges>      // for ranges::*

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

using namespace std::string_view_literals;

namespace gucc::user {

auto create_group(std::string_view group, std::string_view mountpoint, bool is_system) noexcept -> Result<void> {
    // TODO(vnepogodin):
    // 1. add parameter to check if the group was already created
    // 2. add parameter if the group should be --system group

    // --force is used to exit successfully if the group already exists
    const auto& cmd = fmt::format(FMT_COMPILE("groupadd --force {}{}"), is_system ? "--system"sv : ""sv, group);
    if (!utils::arch_chroot_checked(cmd, mountpoint)) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("failed to create group {}", group));
    }
    return {};
}

auto set_user_password(std::string_view username, std::string_view password, std::string_view mountpoint) noexcept -> Result<void> {
    // TODO(vnepogodin): should encrypt user password properly here
    const auto& encrypted_passwd = utils::exec(fmt::format(FMT_COMPILE("openssl passwd {}"), password));
    const auto& password_set_cmd = fmt::format(FMT_COMPILE("usermod -p '{}' {}"), encrypted_passwd, username);
    if (!utils::arch_chroot_checked(password_set_cmd, mountpoint)) {
        spdlog::error("Failed to set password for user {}", username);
        return make_error(ErrorCode::SubprocessFailed, fmt::format("failed to set password for user {}", username));
    }
    return {};
}

auto create_new_user(const user::UserInfo& user_info, const std::vector<std::string>& default_groups, std::string_view mountpoint) noexcept -> Result<void> {
    if (!user_info.sudoers_group.empty() && !std::ranges::contains(default_groups, user_info.sudoers_group)) {
        spdlog::error("Failed to create user {}! User default groups doesn't contain sudoers group({})", user_info.username, user_info.sudoers_group);
        return make_error(ErrorCode::InvalidArgument, fmt::format("user default groups doesn't contain sudoers group {}", user_info.sudoers_group));
    }

    // Create needed groups
    for (const auto& default_group : default_groups) {
        if (auto res = user::create_group(default_group, mountpoint); !res) {
            spdlog::error("Failed to create group {}", default_group);
            return res;
        }
    }

    // Create the user
    spdlog::info("Creating user {}", user_info.username);
    const auto& usercmd = [](auto&& username, auto&& user_shell) -> std::string {
        using namespace std::string_view_literals;
        static constexpr auto USER_BASE_CMD = "useradd -m -U"sv;
        if (!user_shell.empty()) {
            return fmt::format(FMT_COMPILE("{} -s {} {}"), USER_BASE_CMD, user_shell, username);
        }
        return fmt::format(FMT_COMPILE("{} {}"), USER_BASE_CMD, username);
    }(user_info.username, user_info.shell);

    if (!utils::arch_chroot_checked(usercmd, mountpoint)) {
        spdlog::error("Failed to create user with {}", usercmd);
        return make_error(ErrorCode::SubprocessFailed, fmt::format("failed to create user {}", user_info.username));
    }

    // Set user groups
    spdlog::info("Setting groups for user {}", user_info.username);
    const auto& groups_set_cmd = fmt::format(FMT_COMPILE("usermod -aG {} {}"), utils::join(default_groups, ','), user_info.username);
    if (!utils::arch_chroot_checked(groups_set_cmd, mountpoint)) {
        spdlog::error("Failed to set user groups with {}", groups_set_cmd);
        return make_error(ErrorCode::SubprocessFailed, fmt::format("failed to set groups for user {}", user_info.username));
    }

    // Setup user permissions
    spdlog::info("Setting user permissions for {}", user_info.username);
    const auto& user_group   = fmt::format(FMT_COMPILE("{0}:{0}"), user_info.username);
    const auto& user_homedir = fmt::format(FMT_COMPILE("/home/{}"), user_info.username);
    const auto& setup_cmd    = fmt::format(FMT_COMPILE("chown -R {} {}"), user_group, user_homedir);
    if (!utils::arch_chroot_checked(setup_cmd, mountpoint)) {
        spdlog::error("Failed to setup user permissions on {} as {}", user_homedir, user_group);
        return make_error(ErrorCode::SubprocessFailed, fmt::format("failed to setup permissions on {}", user_homedir));
    }

    // Set user password
    if (auto res = set_user_password(user_info.username, user_info.password, mountpoint); !res) {
        return res;
    }

    // Setup sudoers
    if (user_info.sudoers_group.empty()) {
        spdlog::info("skipping sudoers group is empty");
        return {};
    }

    const auto& sudoers_filepath = fmt::format(FMT_COMPILE("{}/etc/sudoers.d/10-installer"), mountpoint);
    {
        const auto& sudoers_line = fmt::format(FMT_COMPILE("%{} ALL=(ALL) ALL\n"), user_info.sudoers_group);
        if (!file_utils::create_file_for_overwrite(sudoers_filepath, sudoers_line)) {
            spdlog::error("Failed to open sudoers for writing {}", sudoers_filepath);
            return make_error(ErrorCode::FileIo, fmt::format("failed to write sudoers file {}", sudoers_filepath));
        }
    }

    std::error_code err{};
    fs::permissions(sudoers_filepath,
        fs::perms::owner_read | fs::perms::group_read,  // 0440
        fs::perm_options::replace, err);
    if (err) {
        spdlog::error("Failed to set permissions for sudoers file: {}", err.message());
        return make_error(ErrorCode::FileIo, fmt::format("failed to set permissions for sudoers file: {}", err.message()));
    }
    return {};
}

auto set_hostname(std::string_view hostname, std::string_view mountpoint) noexcept -> Result<void> {
    {
        const auto& hostname_filepath = fmt::format(FMT_COMPILE("{}/etc/hostname"), mountpoint);
        const auto& hostname_line     = fmt::format(FMT_COMPILE("{}\n"), hostname);
        if (!file_utils::create_file_for_overwrite(hostname_filepath, hostname_line)) {
            spdlog::error("Failed to open hostname for writing {}", hostname_filepath);
            return make_error(ErrorCode::FileIo, fmt::format("failed to write hostname file {}", hostname_filepath));
        }
    }

    if (auto res = user::set_hosts(hostname, mountpoint); !res) {
        spdlog::error("Failed to set hosts");
        return res;
    }
    return {};
}

auto set_hosts(std::string_view hostname, std::string_view mountpoint) noexcept -> Result<void> {
    static constexpr auto STANDARD_HOSTS = R"(# Standard host addresses
127.0.0.1  localhost
::1        localhost ip6-localhost ip6-loopback
ff02::1    ip6-allnodes
ff02::2    ip6-allrouters
)"sv;
    static constexpr auto REQUESTED_HOST = R"(# This host address
127.0.1.1  {}
)";

    {
        const auto& hosts_filepath = fmt::format(FMT_COMPILE("{}/etc/hosts"), mountpoint);
        const auto& hosts_text     = fmt::format(FMT_COMPILE("{}{}"), STANDARD_HOSTS, hostname.empty() ? std::string{} : fmt::format(REQUESTED_HOST, hostname));
        if (!file_utils::create_file_for_overwrite(hosts_filepath, hosts_text)) {
            spdlog::error("Failed to open hosts for writing {}", hosts_filepath);
            return make_error(ErrorCode::FileIo, fmt::format("failed to write hosts file {}", hosts_filepath));
        }
    }
    return {};
}

auto set_root_password(std::string_view password, std::string_view mountpoint) noexcept -> Result<void> {
    return set_user_password("root"sv, password, mountpoint);
}

auto setup_user_environment(const UserAllInfo& info, std::string_view mountpoint) noexcept -> Result<void> {
    spdlog::info("Starting to setup user environment");
    if (auto res = set_hostname(info.hostname, mountpoint); !res) {
        spdlog::error("Failed to set hostname to {}", info.hostname);
        return res;
    }

    if (!info.root_password.empty()) {
        if (auto res = set_root_password(info.root_password, mountpoint); !res) {
            spdlog::error("Failed to set root password");
            return res;
        }
    }

    // create all users at once
    for (auto&& user_info : info.users_info) {
        if (auto res = create_new_user(user_info, info.default_groups, mountpoint); !res) {
            spdlog::error("Failed to create new user {}", user_info.username);
            return res;
        }
    }

    // only single user is supported for autologin
    if (info.autologin && !info.display_manager.empty() && !info.users_info.empty()) {
        const auto& user_info = info.users_info[0];
        if (info.users_info.size() > 1) {
            spdlog::warn("Only single user is supported for autologin! Setting autologin for {}", user_info.username);
        }
        if (!enable_autologin(info.display_manager, user_info.username, mountpoint)) {
            spdlog::error("Failed to enable autologin for user {}", user_info.username);
            return make_error(ErrorCode::SubprocessFailed, fmt::format("failed to enable autologin for user {}", user_info.username));
        }
    }

    spdlog::info("Finished setting up user environment");
    return {};
}

}  // namespace gucc::user
