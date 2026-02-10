#include "installer_config.hpp"

#include <algorithm>    // for
#include <expected>     // for expected, unexpected
#include <ranges>       // for ranges::*
#include <string_view>  // for string_view

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_view_literals;

namespace installer {

auto partition_type_from_string(std::string_view type_str) noexcept
    -> std::optional<PartitionType> {
    if (type_str == "root"sv) {
        return PartitionType::Root;
    }
    if (type_str == "boot"sv) {
        return PartitionType::Boot;
    }
    if (type_str == "additional"sv) {
        return PartitionType::Additional;
    }
    return std::nullopt;
}

auto partition_type_to_string(PartitionType type) noexcept -> std::string_view {
    switch (type) {
    case PartitionType::Root:
        return "root"sv;
    case PartitionType::Boot:
        return "boot"sv;
    case PartitionType::Additional:
        return "additional"sv;
    }
    return "unknown"sv;
}

auto get_default_config() noexcept -> InstallerConfig {
    return InstallerConfig{
        .menus         = 2,
        .headless_mode = false,
        .server_mode   = false,
    };
}

auto parse_installer_config(std::string_view json_content) noexcept
    -> std::expected<InstallerConfig, std::string> {
    if (json_content.empty()) {
        return get_default_config();
    }

    rapidjson::Document doc;
    doc.Parse(json_content.data(), json_content.size());
    if (doc.HasParseError()) {
        return std::unexpected(fmt::format(FMT_COMPILE("JSON parse error at offset {}"), doc.GetErrorOffset()));
    }
    if (!doc.IsObject()) {
        return std::unexpected("JSON root must be an object");
    }

    InstallerConfig config{};

    // Parse menus (required)
    if (!doc.HasMember("menus") || !doc["menus"].IsInt()) {
        return std::unexpected("'menus' field is required and must be an integer");
    }
    config.menus = doc["menus"].GetInt();

    // Parse headless_mode (optional, default false)
    if (doc.HasMember("headless_mode")) {
        if (!doc["headless_mode"].IsBool()) {
            return std::unexpected("'headless_mode' must be a boolean");
        }
        config.headless_mode = doc["headless_mode"].GetBool();
    }

    // Parse server_mode (optional, default false)
    if (doc.HasMember("server_mode")) {
        if (!doc["server_mode"].IsBool()) {
            return std::unexpected("'server_mode' must be a boolean");
        }
        config.server_mode = doc["server_mode"].GetBool();
    }

    // Parse device (optional, but required in headless mode)
    if (doc.HasMember("device")) {
        if (!doc["device"].IsString()) {
            return std::unexpected("'device' must be a string");
        }
        config.device = doc["device"].GetString();
    }

    // Parse fs_name (optional, but required in headless mode)
    if (doc.HasMember("fs_name")) {
        if (!doc["fs_name"].IsString()) {
            return std::unexpected("'fs_name' must be a string");
        }
        config.fs_name = doc["fs_name"].GetString();
    }

    // Parse partitions (optional, but required in headless mode)
    if (doc.HasMember("partitions")) {
        if (!doc["partitions"].IsArray()) {
            return std::unexpected("'partitions' must be an array");
        }

        for (const auto& part_value : doc["partitions"].GetArray()) {
            if (!part_value.IsObject()) {
                return std::unexpected("Each partition must be an object");
            }

            const auto& part_obj = part_value.GetObject();

            // Validate required partition fields
            if (!part_obj.HasMember("name") || !part_obj["name"].IsString()) {
                return std::unexpected("Partition 'name' is required and must be a string");
            }
            if (!part_obj.HasMember("mountpoint") || !part_obj["mountpoint"].IsString()) {
                return std::unexpected("Partition 'mountpoint' is required and must be a string");
            }
            if (!part_obj.HasMember("size") || !part_obj["size"].IsString()) {
                return std::unexpected("Partition 'size' is required and must be a string");
            }
            if (!part_obj.HasMember("type") || !part_obj["type"].IsString()) {
                return std::unexpected("Partition 'type' is required and must be a string");
            }

            PartitionConfig part_config{};
            part_config.name       = part_obj["name"].GetString();
            part_config.mountpoint = part_obj["mountpoint"].GetString();
            part_config.size       = part_obj["size"].GetString();

            // Validate and parse partition type
            const auto& type_str = std::string_view{part_obj["type"].GetString()};
            auto part_type       = partition_type_from_string(type_str);
            if (!part_type) {
                return std::unexpected(fmt::format(FMT_COMPILE("Invalid partition type '{}'. Valid types: root, boot, additional"), type_str));
            }
            part_config.type = *part_type;

            // Parse fs_name (optional for root if global fs_name is set)
            if (part_obj.HasMember("fs_name")) {
                if (!part_obj["fs_name"].IsString()) {
                    return std::unexpected("Partition 'fs_name' must be a string");
                }
                part_config.fs_name = part_obj["fs_name"].GetString();
            } else if (part_config.type == PartitionType::Root && config.fs_name) {
                // For root partition, inherit from global fs_name if not specified
                part_config.fs_name = *config.fs_name;
            } else if (part_config.type != PartitionType::Root) {
                return std::unexpected(fmt::format(FMT_COMPILE("'fs_name' is required for partition type '{}'"), type_str));
            } else {
                return std::unexpected("'fs_name' is required for root partition when global fs_name is not set");
            }

            config.partitions.push_back(std::move(part_config));
        }
    }

    // Parse mount_opts (optional)
    if (doc.HasMember("mount_opts")) {
        if (!doc["mount_opts"].IsString()) {
            return std::unexpected("'mount_opts' must be a string");
        }
        config.mount_opts = doc["mount_opts"].GetString();
    }

    // Parse hostname (optional, but required in headless mode)
    if (doc.HasMember("hostname")) {
        if (!doc["hostname"].IsString()) {
            return std::unexpected("'hostname' must be a string");
        }
        config.hostname = doc["hostname"].GetString();
    }

    // Parse locale (optional, but required in headless mode)
    if (doc.HasMember("locale")) {
        if (!doc["locale"].IsString()) {
            return std::unexpected("'locale' must be a string");
        }
        config.locale = doc["locale"].GetString();
    }

    // Parse xkbmap (optional, but required in headless mode)
    if (doc.HasMember("xkbmap")) {
        if (!doc["xkbmap"].IsString()) {
            return std::unexpected("'xkbmap' must be a string");
        }
        config.xkbmap = doc["xkbmap"].GetString();
    }

    // Parse timezone (optional, but required in headless mode)
    if (doc.HasMember("timezone")) {
        if (!doc["timezone"].IsString()) {
            return std::unexpected("'timezone' must be a string");
        }
        config.timezone = doc["timezone"].GetString();
    }

    // Parse user settings
    if (doc.HasMember("user_name")) {
        if (!doc["user_name"].IsString()) {
            return std::unexpected("'user_name' must be a string");
        }
        config.user_name = doc["user_name"].GetString();
    }

    if (doc.HasMember("user_pass")) {
        if (!doc["user_pass"].IsString()) {
            return std::unexpected("'user_pass' must be a string");
        }
        config.user_pass = doc["user_pass"].GetString();
    }

    if (doc.HasMember("user_shell")) {
        if (!doc["user_shell"].IsString()) {
            return std::unexpected("'user_shell' must be a string");
        }
        config.user_shell = doc["user_shell"].GetString();
    }

    if (doc.HasMember("root_pass")) {
        if (!doc["root_pass"].IsString()) {
            return std::unexpected("'root_pass' must be a string");
        }
        config.root_pass = doc["root_pass"].GetString();
    }

    // Parse kernel (optional, but required in headless mode)
    if (doc.HasMember("kernel")) {
        if (!doc["kernel"].IsString()) {
            return std::unexpected("'kernel' must be a string");
        }
        config.kernel = doc["kernel"].GetString();
    }

    // Parse desktop (optional, but required in headless mode)
    if (doc.HasMember("desktop")) {
        if (!doc["desktop"].IsString()) {
            return std::unexpected("'desktop' must be a string");
        }
        config.desktop = doc["desktop"].GetString();
    }

    // Parse bootloader (optional, but required in headless mode)
    if (doc.HasMember("bootloader")) {
        if (!doc["bootloader"].IsString()) {
            return std::unexpected("'bootloader' must be a string");
        }
        config.bootloader = doc["bootloader"].GetString();
    }

    // Parse post_install (optional)
    if (doc.HasMember("post_install")) {
        if (!doc["post_install"].IsString()) {
            return std::unexpected("'post_install' must be a string");
        }
        config.post_install = doc["post_install"].GetString();
    }

    return config;
}

auto validate_headless_config(const InstallerConfig& config) noexcept
    -> std::expected<void, std::string> {
    if (!config.headless_mode) {
        return {};
    }

    std::string missing_fields;
    if (!config.device) {
        missing_fields += "'device', ";
    }
    if (!config.fs_name) {
        missing_fields += "'fs_name', ";
    }
    if (config.partitions.empty()) {
        missing_fields += "'partitions', ";
    }
    if (!config.hostname) {
        missing_fields += "'hostname', ";
    }
    if (!config.locale) {
        missing_fields += "'locale', ";
    }
    if (!config.xkbmap) {
        missing_fields += "'xkbmap', ";
    }
    if (!config.timezone) {
        missing_fields += "'timezone', ";
    }
    if (!config.user_name || !config.user_pass || !config.user_shell) {
        missing_fields += "'user_name', 'user_pass', 'user_shell', ";
    }
    if (!config.root_pass) {
        missing_fields += "'root_pass', ";
    }
    if (!config.kernel) {
        missing_fields += "'kernel', ";
    }
    if (!config.desktop) {
        missing_fields += "'desktop', ";
    }
    if (!config.bootloader) {
        missing_fields += "'bootloader', ";
    }

    if (!missing_fields.empty()) {
        missing_fields.resize(missing_fields.size() - 2);
        return std::unexpected(fmt::format(FMT_COMPILE("HEADLESS mode requires: {}"), missing_fields));
    }

    return {};
}

}  // namespace installer
