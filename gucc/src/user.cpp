#include "gucc/user.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>  // for find
#include <filesystem>

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <range/v3/algorithm/contains.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace fs = std::filesystem;

using namespace std::string_view_literals;

namespace gucc::user {

auto create_group(std::string_view group, std::string_view mountpoint, bool is_system) noexcept -> bool {
    // TODO(vnepogodin):
    // 1. add parameter to check if the group was already created
    // 2. add parameter if the group should be --system group
    const auto& cmd = fmt::format(FMT_COMPILE("groupadd {}{}"), is_system ? "--system"sv : ""sv, group);
    return utils::arch_chroot_checked(cmd, mountpoint);
}

auto set_user_password(std::string_view username, std::string_view password, std::string_view mountpoint) noexcept -> bool {
    // TODO(vnepogodin): should encrypt user password properly here
    const auto& encrypted_passwd = utils::exec(fmt::format(FMT_COMPILE("openssl passwd {}"), password));
    const auto& password_set_cmd = fmt::format(FMT_COMPILE("usermod -p '{}' {}"), encrypted_passwd, username);
    if (!utils::arch_chroot_checked(password_set_cmd, mountpoint)) {
        spdlog::error("Failed to set password for user {}", username);
        return false;
    }
    return true;
}

auto create_new_user(const user::UserInfo& user_info, const std::vector<std::string>& default_groups, std::string_view mountpoint) noexcept -> bool {
    if (!user_info.sudoers_group.empty() && !ranges::contains(default_groups, user_info.sudoers_group)) {
        spdlog::error("Failed to create user {}! User default groups doesn't contain sudoers group({})", user_info.username, user_info.sudoers_group);
        return false;
    }

    // Create needed groups
    for (const auto& default_group : default_groups) {
        if (!user::create_group(default_group, mountpoint)) {
            spdlog::error("Failed to create group {}", default_group);
            return false;
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
        return false;
    }

    // Set user groups
    spdlog::info("Setting groups for user {}", user_info.username);
    const auto& groups_set_cmd = fmt::format(FMT_COMPILE("usermod -aG {} {}"), utils::join(default_groups, ","), user_info.username);
    if (!utils::arch_chroot_checked(groups_set_cmd, mountpoint)) {
        spdlog::error("Failed to set user groups with {}", groups_set_cmd);
        return false;
    }

    // Setup user permissions
    spdlog::info("Setting user permissions for {}", user_info.username);
    const auto& user_group   = fmt::format(FMT_COMPILE("{0}:{0}"), user_info.username);
    const auto& user_homedir = fmt::format(FMT_COMPILE("/home/{}"), user_info.username);
    const auto& setup_cmd    = fmt::format(FMT_COMPILE("chown -R {} {}"), user_group, user_homedir);
    if (!utils::arch_chroot_checked(setup_cmd, mountpoint)) {
        spdlog::error("Failed to setup user permissions on {} as {}", user_homedir, user_group);
        return false;
    }

    // Set user password
    if (!set_user_password(user_info.username, user_info.password, mountpoint)) {
        return false;
    }

    // Setup sudoers
    if (user_info.sudoers_group.empty()) {
        spdlog::info("skipping sudoers group is empty");
        return true;
    }

    const auto& sudoers_filepath = fmt::format(FMT_COMPILE("{}/etc/sudoers.d/10-installer"), mountpoint);
    {
        const auto& sudoers_line = fmt::format(FMT_COMPILE("%{} ALL=(ALL) ALL\n"), user_info.sudoers_group);
        if (!file_utils::create_file_for_overwrite(sudoers_filepath, sudoers_line)) {
            spdlog::error("Failed to open sudoers for writing {}", sudoers_filepath);
            return false;
        }
    }

    std::error_code err{};
    fs::permissions(sudoers_filepath,
        fs::perms::owner_read | fs::perms::group_read,  // 0440
        fs::perm_options::replace, err);
    if (err) {
        spdlog::error("Failed to set permissions for sudoers file: {}", err.message());
        return false;
    }
    return true;
}

auto set_hostname(std::string_view hostname, std::string_view mountpoint) noexcept -> bool {
    {
        const auto& hostname_filepath = fmt::format(FMT_COMPILE("{}/etc/hostname"), mountpoint);
        const auto& hostname_line     = fmt::format(FMT_COMPILE("{}\n"), hostname);
        if (!file_utils::create_file_for_overwrite(hostname_filepath, hostname_line)) {
            spdlog::error("Failed to open hostname for writing {}", hostname_filepath);
            return false;
        }
    }

    if (!user::set_hosts(hostname, mountpoint)) {
        spdlog::error("Failed to set hosts");
        return false;
    }
    return true;
}

auto set_hosts(std::string_view hostname, std::string_view mountpoint) noexcept -> bool {
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
            return false;
        }
    }
    return true;
}

auto set_root_password(std::string_view password, std::string_view mountpoint) noexcept -> bool {
    return set_user_password("root"sv, password, mountpoint);
}

}  // namespace gucc::user
