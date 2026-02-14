#include "gucc/systemd_homed.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <string>  // for string
#include <vector>  // for vector

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::homed {

auto storage_type_to_string(StorageType type) noexcept -> std::string_view {
    switch (type) {
    case StorageType::Luks:
        return "luks"sv;
    case StorageType::Fscrypt:
        return "fscrypt"sv;
    case StorageType::Directory:
        return "directory"sv;
    case StorageType::Subvolume:
        return "subvolume"sv;
    case StorageType::Cifs:
        return "cifs"sv;
    }
    return "luks"sv;
}

auto luks_fs_type_to_string(LuksFilesystemType type) noexcept -> std::string_view {
    switch (type) {
    case LuksFilesystemType::Ext4:
        return "ext4"sv;
    case LuksFilesystemType::Btrfs:
        return "btrfs"sv;
    case LuksFilesystemType::Xfs:
        return "xfs"sv;
    }
    return "ext4"sv;
}

auto generate_homectl_args(const HomedUserConfig& config) noexcept -> std::vector<std::string> {
    std::vector<std::string> args;

    args.emplace_back(fmt::format(FMT_COMPILE("--storage={}"), storage_type_to_string(config.storage)));
    if (!config.real_name.empty()) {
        args.emplace_back(fmt::format(FMT_COMPILE("--real-name={}"), config.real_name));
    }
    if (!config.shell.empty()) {
        args.emplace_back(fmt::format(FMT_COMPILE("--shell={}"), config.shell));
    }
    if (config.uid.has_value()) {
        args.emplace_back(fmt::format(FMT_COMPILE("--uid={}"), *config.uid));
    }
    if (!config.groups.empty()) {
        args.emplace_back(fmt::format(FMT_COMPILE("--member-of={}"), utils::join(config.groups, ',')));
    }
    if (config.image_path.has_value()) {
        args.emplace_back(fmt::format(FMT_COMPILE("--image-path={}"), *config.image_path));
    }
    if (config.home_dir.has_value()) {
        args.emplace_back(fmt::format(FMT_COMPILE("--home-dir={}"), *config.home_dir));
    }

    // LUKS-specific options
    if (config.storage == StorageType::Luks) {
        // Filesystem type
        args.emplace_back(fmt::format(FMT_COMPILE("--fs-type={}"), luks_fs_type_to_string(config.luks_fs_type)));

        // Disk size
        if (config.disk_size.has_value()) {
            args.emplace_back(fmt::format(FMT_COMPILE("--disk-size={}"), *config.disk_size));
        }

        // Discard settings
        args.emplace_back(fmt::format(FMT_COMPILE("--luks-discard={}"), config.luks_discard ? "true" : "false"));
        args.emplace_back(fmt::format(FMT_COMPILE("--luks-offline-discard={}"), config.luks_offline_discard ? "true" : "false"));
    }

    return args;
}

auto create_homed_user(const HomedUserConfig& config) noexcept -> bool {
    if (config.username.empty() || config.password.empty()) {
        spdlog::error("Cannot create homed user: username or password empty");
        return false;
    }

    spdlog::info("Creating homed user: {}", config.username);
    const auto& args = utils::join(generate_homectl_args(config), ' ');
    const auto& cmd  = fmt::format(FMT_COMPILE("NEWPASSWORD='{}' homectl create {} --password-change-now=true {}"), config.password, config.username, args);
    if (!utils::exec_checked(cmd)) {
        spdlog::error("Failed to create homed user: {}", config.username);
        return false;
    }
    return true;
}

auto activate_home(std::string_view username) noexcept -> bool {
    if (username.empty()) {
        spdlog::error("Cannot activate home: username is empty");
        return false;
    }

    spdlog::info("Activating home for user: {}", username);
    const auto& cmd = fmt::format(FMT_COMPILE("homectl activate {}"), username);
    if (!utils::exec_checked(cmd)) {
        spdlog::error("Failed to activate home for user: {}", username);
        return false;
    }
    return true;
}

auto deactivate_home(std::string_view username) noexcept -> bool {
    if (username.empty()) {
        spdlog::error("Cannot deactivate home: username is empty");
        return false;
    }

    spdlog::info("Deactivating home for user: {}", username);
    const auto& cmd = fmt::format(FMT_COMPILE("homectl deactivate {}"), username);
    if (!utils::exec_checked(cmd)) {
        spdlog::error("Failed to deactivate home for user: {}", username);
        return false;
    }
    return true;
}

auto remove_homed_user(std::string_view username) noexcept -> bool {
    if (username.empty()) {
        spdlog::error("Cannot remove homed user: username is empty");
        return false;
    }

    spdlog::info("Removing homed user: {}", username);
    const auto& cmd = fmt::format(FMT_COMPILE("homectl remove {}"), username);
    if (!utils::exec_checked(cmd)) {
        spdlog::error("Failed to remove homed user: {}", username);
        return false;
    }
    return true;
}

}  // namespace gucc::homed
