#include "cachyos/installer_config.hpp"

#include "cachyos/headless_plan.hpp"
#include "cachyos/system.hpp"

// import gucc
#include "gucc/bootloader.hpp"

#include <expected>          // for expected, unexpected
#include <initializer_list>  // for initializer_list
#include <optional>          // for optional
#include <ranges>            // for ranges::*
#include <string_view>       // for string_view
#include <utility>           // for pair, move

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
#include <fmt/ranges.h>

using namespace std::string_view_literals;

namespace {

[[nodiscard]] auto bootloader_from_name(const std::optional<std::string>& name) noexcept
    -> gucc::bootloader::BootloaderType {
    if (!name) {
        return gucc::bootloader::BootloaderType::Grub;
    }
    const auto parsed = gucc::bootloader::bootloader_from_string(*name);
    return parsed.value_or(gucc::bootloader::BootloaderType::Grub);
}

auto parse_optional_bool(auto&& doc, const char* key, bool& out) noexcept -> std::optional<std::string> {
    if (!doc.HasMember(key)) {
        return std::nullopt;
    }
    if (!doc[key].IsBool()) {
        return fmt::format(FMT_COMPILE("'{}' must be a boolean"), key);
    }
    out = doc[key].GetBool();
    return std::nullopt;
}

auto parse_optional_string(auto&& doc, const char* key, std::optional<std::string>& out) noexcept -> std::optional<std::string> {
    if (!doc.HasMember(key)) {
        return std::nullopt;
    }
    if (!doc[key].IsString()) {
        return fmt::format(FMT_COMPILE("'{}' must be a string"), key);
    }
    out = doc[key].GetString();
    return std::nullopt;
}

}  // namespace

namespace cachyos::installer {

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

    for (const auto& [key, out] : std::initializer_list<std::pair<const char*, bool*>>{
             {"headless_mode", &config.headless_mode},
             {"server_mode", &config.server_mode},
             {"allow_auto_partition", &config.allow_auto_partition},
             {"encrypt_swap", &config.encrypt_swap},
             {"hostcache", &config.hostcache},
         }) {
        if (auto err = parse_optional_bool(doc, key, *out)) {
            return std::unexpected(std::move(*err));
        }
    }

    for (const auto& [key, out] : std::initializer_list<std::pair<const char*, std::optional<std::string>*>>{
             {"device", &config.device},
             {"fs_name", &config.fs_name},
             {"mount_opts", &config.mount_opts},
             {"hostname", &config.hostname},
             {"locale", &config.locale},
             {"xkbmap", &config.xkbmap},
             {"timezone", &config.timezone},
             {"user_name", &config.user_name},
             {"user_pass", &config.user_pass},
             {"user_shell", &config.user_shell},
             {"root_pass", &config.root_pass},
             {"kernel", &config.kernel},
             {"desktop", &config.desktop},
             {"bootloader", &config.bootloader},
             {"post_install", &config.post_install},
         }) {
        if (auto err = parse_optional_string(doc, key, *out)) {
            return std::unexpected(std::move(*err));
        }
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

    // Parse subvolumes (optional)
    if (doc.HasMember("subvolumes")) {
        if (doc["subvolumes"].IsString()) {
            const auto& subvols_str = std::string_view{doc["subvolumes"].GetString()};
            if (!subvols_str.empty() && subvols_str != "default"sv) {
                return std::unexpected(fmt::format(FMT_COMPILE("'subvolumes' string must be 'default', got '{}'"), subvols_str));
            }
            config.use_default_subvolumes = true;
        } else if (doc["subvolumes"].IsArray()) {
            config.use_default_subvolumes = false;
            for (const auto& subvol_value : doc["subvolumes"].GetArray()) {
                if (!subvol_value.IsObject()) {
                    return std::unexpected("Each subvolume entry must be an object");
                }

                const auto& subvol_obj = subvol_value.GetObject();
                if (!subvol_obj.HasMember("subvolume") || !subvol_obj["subvolume"].IsString()) {
                    return std::unexpected("Subvolume 'subvolume' is required and must be a string");
                }
                if (!subvol_obj.HasMember("mountpoint") || !subvol_obj["mountpoint"].IsString()) {
                    return std::unexpected("Subvolume 'mountpoint' is required and must be a string");
                }

                SubvolumeConfig subvol_config{
                    .subvolume  = subvol_obj["subvolume"].GetString(),
                    .mountpoint = subvol_obj["mountpoint"].GetString(),
                };
                config.subvolumes.push_back(std::move(subvol_config));
            }
        } else {
            return std::unexpected("'subvolumes' must be a string (\"default\") or an array");
        }
    }

    return config;
}

auto validate_headless_config(const InstallerConfig& config) noexcept
    -> std::expected<void, std::string> {
    if (!config.headless_mode) {
        return {};
    }

    const bool needs_layout = !config.allow_auto_partition;

    const auto is_present = std::initializer_list<std::pair<std::string_view, bool>>{
        {"'device'"sv, config.device.has_value()},
        {"'fs_name'"sv, config.fs_name.has_value()},
        {"'partitions'"sv, !needs_layout || !config.partitions.empty()},
        {"'hostname'"sv, config.hostname.has_value()},
        {"'locale'"sv, config.locale.has_value()},
        {"'xkbmap'"sv, config.xkbmap.has_value()},
        {"'timezone'"sv, config.timezone.has_value()},
        {"'user_name', 'user_pass', 'user_shell'"sv,
            config.user_name.has_value() && config.user_pass.has_value() && config.user_shell.has_value()},
        {"'root_pass'"sv, config.root_pass.has_value()},
        {"'kernel'"sv, config.kernel.has_value()},
        {"'desktop'"sv, config.desktop.has_value()},
        {"'bootloader'"sv, config.bootloader.has_value()},
    };

    const auto missing_fields = is_present
        | std::ranges::views::filter([](const auto& entry) { return !entry.second; })
        | std::ranges::views::keys
        | std::ranges::to<std::vector<std::string_view>>();

    if (!missing_fields.empty()) {
        return std::unexpected(fmt::format(FMT_COMPILE("HEADLESS mode requires: {}"), fmt::join(missing_fields, ", ")));
    }

    return {};
}

auto installer_config_to_inputs(const InstallerConfig& cfg) noexcept
    -> std::expected<cachyos::installer::InstallerInputs, std::string> {
    cachyos::installer::InstallerInputs inputs{};

    const auto sysinfo = cachyos::installer::detect_system();
    if (!sysinfo) {
        return std::unexpected(fmt::format("detect_system failed: {}", sysinfo.error()));
    }
    inputs.ctx.system_mode = sysinfo->system_mode;

    const auto is_efi = sysinfo->system_mode == InstallContext::SystemMode::UEFI;
    auto strategy     = headless_strategy_from_config(cfg, is_efi);
    if (!strategy) {
        return std::unexpected(fmt::format(FMT_COMPILE("invalid partition configuration:\n  - {}"),
            fmt::join(strategy.error(), "\n  - ")));
    }
    inputs.ctx.strategy = std::move(*strategy);

    inputs.ctx.device          = cfg.device.value_or("");
    inputs.ctx.filesystem_name = cfg.fs_name.value_or("");
    inputs.ctx.server_mode     = cfg.server_mode;
    inputs.ctx.kernel          = cfg.kernel.value_or("");
    inputs.ctx.desktop         = cfg.desktop.value_or("");
    inputs.ctx.bootloader      = bootloader_from_name(cfg.bootloader);
    inputs.ctx.encrypt_swap    = cfg.encrypt_swap;
    inputs.ctx.hostcache       = cfg.hostcache;

    if (cfg.xkbmap) {
        inputs.ctx.keymap = *cfg.xkbmap;
    }

    inputs.sys.hostname = cfg.hostname.value_or("");
    inputs.sys.locale   = cfg.locale.value_or("");
    inputs.sys.xkbmap   = cfg.xkbmap.value_or("");
    inputs.sys.keymap   = cfg.xkbmap.value_or("");
    inputs.sys.timezone = cfg.timezone.value_or("");

    inputs.user.username = cfg.user_name.value_or("");
    inputs.user.password = cfg.user_pass.value_or("");
    inputs.user.shell    = cfg.user_shell.value_or("");

    inputs.root_password = cfg.root_pass.value_or("");

    return inputs;
}

}  // namespace cachyos::installer
