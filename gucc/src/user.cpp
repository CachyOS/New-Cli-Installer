#include "gucc/user.hpp"
#include "gucc/autologin.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/logger.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>   // for find, contains
#include <filesystem>  // for permissions
#include <ranges>      // for ranges::*
#include <string>      // for string
#include <vector>      // for vector

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace {
auto set_user_groups(const std::vector<std::string>& user_groups, std::string_view username, std::string_view mountpoint) noexcept -> gucc::Result<void> {
    const auto& groups = gucc::utils::join(user_groups, ',');
    const auto argv    = std::vector{"/usr/bin/usermod"s, "-aG"s, groups, std::string{username}};
    return gucc::utils::arch_chroot_or_error(argv, mountpoint, "set user groups"sv);
}
}  // namespace

namespace gucc::user {

auto create_group(std::string_view group, std::string_view mountpoint, bool is_system) noexcept -> Result<void> {
    // TODO(vnepogodin):
    // 1. add parameter to check if the group was already created
    // 2. add parameter if the group should be --system group

    // --force is used to exit successfully if the group already exists
    std::vector<std::string> argv{"/usr/bin/groupadd"s, "--force"s};
    if (is_system) {
        argv.emplace_back("--system"s);
    }
    argv.emplace_back(group);
    return utils::arch_chroot_or_error(argv, mountpoint, "create group"sv);
}

auto set_user_password(std::string_view username, std::string_view password, std::string_view mountpoint) noexcept -> Result<void> {
    const auto hash_output      = utils::exec_with_input("openssl passwd -6 -stdin"sv, password);
    const auto encrypted_passwd = hash_output ? std::string{utils::trim(*hash_output)} : std::string{};
    if (encrypted_passwd.empty()) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format(FMT_COMPILE("failed to hash password for user {}"), username));
    }
    logger::register_secret(encrypted_passwd);

    return utils::arch_chroot_or_error({"/usr/bin/usermod"s, "-p"s, encrypted_passwd, std::string{username}}, mountpoint, "set user password"sv);
}

auto create_new_user(const user::UserInfo& user_info, const std::vector<std::string>& default_groups, std::string_view mountpoint) noexcept -> Result<void> {
    if (!user_info.sudoers_group.empty() && !std::ranges::contains(default_groups, user_info.sudoers_group)) {
        spdlog::error("Failed to create user {}! User default groups doesn't contain sudoers group({})", user_info.username, user_info.sudoers_group);
        return make_error(ErrorCode::InvalidArgument, fmt::format("user default groups doesn't contain sudoers group {}", user_info.sudoers_group));
    }

    // Create needed groups
    for (const auto& default_group : default_groups) {
        if (auto res = user::create_group(default_group, mountpoint); !res) {
            return res;
        }
    }

    // Create the user
    spdlog::info("Creating user {}", user_info.username);
    std::vector<std::string> useradd_argv{"/usr/bin/useradd"s, "-m"s, "-U"s};
    if (!user_info.shell.empty()) {
        useradd_argv.insert(useradd_argv.end(), {"-s"s, std::string{user_info.shell}});
    }
    useradd_argv.emplace_back(user_info.username);

    if (auto res = utils::arch_chroot_or_error(useradd_argv, mountpoint, "create user"sv); !res) {
        return res;
    }

    // Set user groups
    spdlog::info("Setting groups for user {}", user_info.username);
    if (auto res = set_user_groups(default_groups, user_info.username, mountpoint); !res) {
        return res;
    }

    // Setup user permissions
    spdlog::info("Setting user permissions for {}", user_info.username);
    const auto& user_group   = fmt::format(FMT_COMPILE("{0}:{0}"), user_info.username);
    const auto& user_homedir = fmt::format(FMT_COMPILE("/home/{}"), user_info.username);
    if (auto res = utils::arch_chroot_or_error({"/usr/bin/chown"s, "-R"s, user_group, user_homedir}, mountpoint, "set home permissions"sv); !res) {
        return res;
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
    const auto& sudoers_line     = fmt::format(FMT_COMPILE("%{} ALL=(ALL) ALL\n"), user_info.sudoers_group);
    if (auto res = file_utils::write_file(sudoers_filepath, sudoers_line); !res) {
        return res;
    }

    std::error_code err{};
    fs::permissions(sudoers_filepath,
        fs::perms::owner_read | fs::perms::group_read,  // 0440
        fs::perm_options::replace, err);
    if (err) {
        return make_error(ErrorCode::FileIo, fmt::format(FMT_COMPILE("failed to set permissions for sudoers file: {}"), err.message()));
    }
    return {};
}

auto set_hostname(std::string_view hostname, std::string_view mountpoint) noexcept -> Result<void> {
    const auto& hostname_filepath = fmt::format(FMT_COMPILE("{}/etc/hostname"), mountpoint);
    if (auto res = file_utils::write_file(hostname_filepath, fmt::format(FMT_COMPILE("{}\n"), hostname)); !res) {
        return res;
    }
    return user::set_hosts(hostname, mountpoint);
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

    const auto& hosts_filepath = fmt::format(FMT_COMPILE("{}/etc/hosts"), mountpoint);
    const auto& hosts_text     = fmt::format(FMT_COMPILE("{}{}"), STANDARD_HOSTS, hostname.empty() ? std::string{} : fmt::format(REQUESTED_HOST, hostname));
    return file_utils::write_file(hosts_filepath, hosts_text);
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
        if (auto res = enable_autologin(info.display_manager, user_info.username, mountpoint); !res) {
            spdlog::error("Failed to enable autologin for user {}", user_info.username);
            return res;
        }
    }

    spdlog::info("Finished setting up user environment");
    return {};
}

}  // namespace gucc::user
