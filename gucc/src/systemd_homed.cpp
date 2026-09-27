#include "gucc/systemd_homed.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>  // for move
#include <array>      // for array
#include <iterator>   // for back_inserter
#include <ranges>     // for ranges::*
#include <string>     // for string
#include <vector>     // for vector

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

// NOLINTNEXTLINE
using namespace gucc;

auto homectl(std::string_view verb, std::string_view action, std::string_view username) noexcept -> Result<void> {
    if (username.empty()) {
        return make_error(ErrorCode::InvalidArgument, fmt::format(FMT_COMPILE("Cannot {}: username is empty"), action));
    }
    spdlog::info("homectl {} {}", verb, username);
    return utils::exec_or_error({"/usr/bin/homectl"s, std::string{verb}, std::string{username}},
        fmt::format(FMT_COMPILE("Failed to {}: {}"), action, username));
}

}  // namespace

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

auto create_homed_user(const HomedUserConfig& config) noexcept -> Result<void> {
    if (config.username.empty() || config.password.empty()) {
        return make_error(ErrorCode::InvalidArgument, "Cannot create homed user: username or password empty");
    }

    spdlog::info("Creating homed user: {}", config.username);
    // keep secrets to proc env
    const std::array env{fmt::format(FMT_COMPILE("NEWPASSWORD={}"), config.password)};
    std::vector<std::string> argv{
        "/usr/bin/homectl"s,
        "create"s,
        std::string{config.username},
        "--password-change-now=true"s,
    };
    std::ranges::move(generate_homectl_args(config), std::back_inserter(argv));
    return utils::exec_or_error(argv, env, fmt::format(FMT_COMPILE("Failed to create homed user: {}"), config.username));
}

auto activate_home(std::string_view username) noexcept -> Result<void> {
    return homectl("activate"sv, "activate home"sv, username);
}

auto deactivate_home(std::string_view username) noexcept -> Result<void> {
    return homectl("deactivate"sv, "deactivate home"sv, username);
}

auto remove_homed_user(std::string_view username) noexcept -> Result<void> {
    return homectl("remove"sv, "remove homed user"sv, username);
}

}  // namespace gucc::homed
