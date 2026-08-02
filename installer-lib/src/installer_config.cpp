#include "cachyos/installer_config.hpp"

#include "cachyos/headless_plan.hpp"
#include "cachyos/packages.hpp"
#include "cachyos/system.hpp"

// import gucc
#include "gucc/bootloader.hpp"

#include <cstdint>  // for uint16_t

#include <algorithm>         // for contains
#include <array>             // for array
#include <expected>          // for expected, unexpected
#include <initializer_list>  // for initializer_list
#include <optional>          // for optional
#include <ranges>            // for ranges::*
#include <string_view>       // for string_view
#include <tuple>             // for tuple
#include <utility>           // for pair, move
#include <vector>            // for vector

#include <spdlog/spdlog.h>  // for warn

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

// NOTE: all our valid keys
constexpr std::array kKnownKeys{
    "menus"sv,
    "install_type"sv,
    "headless_mode"sv,
    "server_mode"sv,
    "allow_auto_partition"sv,
    "encrypt_swap"sv,
    "hostcache"sv,
    "device"sv,
    "fs_name"sv,
    "mount_opts"sv,
    "partitions"sv,
    "subvolumes"sv,
    "hostname"sv,
    "locale"sv,
    "xkbmap"sv,
    "keymap"sv,
    "timezone"sv,
    "user_name"sv,
    "username"sv,
    "user_pass"sv,
    "user_password"sv,
    "user_shell"sv,
    "root_pass"sv,
    "root_password"sv,
    "kernel"sv,
    "desktop"sv,
    "bootloader"sv,
    "post_install"sv,
    "server_profile"sv,
    "net_profiles_path"sv,
    "ssh_authorized_keys"sv,
    "server_extra_packages"sv,
    "server_extra_tcp_ports"sv,
    "server_extra_udp_ports"sv,
    "autologin"sv,
    "user_groups"sv,
    "hw_clock"sv,
    "chwd"sv,
    "carry_network"sv,
    "os_prober"sv,
    "netinstall_groups"sv,
    "config_version"sv,
};

// NOTE: our valid fs. does not represent the FS support of the installer tho
constexpr std::array kValidFilesystems{
    "ext4"sv,
    "btrfs"sv,
    "xfs"sv,
    "f2fs"sv,
};

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

auto parse_optional_string_array(auto&& doc, const char* key, std::vector<std::string>& out) noexcept -> std::optional<std::string> {
    if (!doc.HasMember(key)) {
        return std::nullopt;
    }
    if (!doc[key].IsArray()) {
        return fmt::format(FMT_COMPILE("'{}' must be an array of strings"), key);
    }
    for (const auto& elem : doc[key].GetArray()) {
        if (!elem.IsString()) {
            return fmt::format(FMT_COMPILE("'{}' must be an array of strings"), key);
        }
        out.emplace_back(elem.GetString());
    }
    return std::nullopt;
}

auto parse_optional_port_array(auto&& doc, const char* key, std::vector<std::uint16_t>& out) noexcept -> std::optional<std::string> {
    if (!doc.HasMember(key)) {
        return std::nullopt;
    }
    if (!doc[key].IsArray()) {
        return fmt::format(FMT_COMPILE("'{}' must be an array of port numbers"), key);
    }
    for (const auto& elem : doc[key].GetArray()) {
        if (!elem.IsInt() || elem.GetInt() < 1 || elem.GetInt() > 65535) {
            return fmt::format(FMT_COMPILE("'{}' must be 1-65535"), key);
        }
        out.emplace_back(static_cast<std::uint16_t>(elem.GetInt()));
    }
    return std::nullopt;
}

auto apply_string_alias(auto&& doc, const char* canonical, const char* alias, std::optional<std::string>& field) noexcept -> std::optional<std::string> {
    if (!doc.HasMember(alias)) {
        return std::nullopt;
    }
    if (doc.HasMember(canonical)) {
        return fmt::format(FMT_COMPILE("'{}' and its alias '{}' cannot both be set"), canonical, alias);
    }
    if (!doc[alias].IsString()) {
        return fmt::format(FMT_COMPILE("'{}' must be a string"), alias);
    }
    field = doc[alias].GetString();
    return std::nullopt;
}

auto collect_unknown_keys(auto&& doc) noexcept -> std::vector<std::string> {
    std::vector<std::string> unknown_keys{};
    for (auto member = doc.MemberBegin(); member != doc.MemberEnd(); ++member) {
        const auto name = std::string_view{member->name.GetString(), member->name.GetStringLength()};
        if (!std::ranges::contains(kKnownKeys, name)) {
            unknown_keys.emplace_back(name);
        }
    }
    return unknown_keys;
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

    // reject unknown keys instead of ignoring them
    if (const auto unknown = collect_unknown_keys(doc); !unknown.empty()) {
        return std::unexpected(fmt::format(FMT_COMPILE("unknown config keys: {}"), fmt::join(unknown, ", ")));
    }

    InstallerConfig config{};

    const bool has_menus        = doc.HasMember("menus");
    const bool has_install_type = doc.HasMember("install_type");
    if (has_menus && has_install_type) {
        return std::unexpected("'menus' and its alias 'install_type' cannot both be set");
    }
    if (has_install_type) {
        if (!doc["install_type"].IsString()) {
            return std::unexpected("'install_type' must be a string ('simple' or 'advanced')");
        }
        const auto value = std::string_view{doc["install_type"].GetString()};
        if (value == "simple"sv) {
            config.menus = 1;
        } else if (value == "advanced"sv) {
            config.menus = 2;
        } else {
            return std::unexpected(fmt::format(FMT_COMPILE("'install_type' must be 'simple' or 'advanced', got '{}'"), value));
        }
    } else {
        if (!has_menus || !doc["menus"].IsInt()) {
            return std::unexpected("'menus' (or its alias 'install_type') is required; 'menus' must be an integer");
        }
        config.menus = doc["menus"].GetInt();
    }

    // TODO(vnepogodin): refactor that shit later

    for (const auto& [key, out] : std::initializer_list<std::pair<const char*, bool*>>{
             {"headless_mode", &config.headless_mode},
             {"server_mode", &config.server_mode},
             {"allow_auto_partition", &config.allow_auto_partition},
             {"encrypt_swap", &config.encrypt_swap},
             {"hostcache", &config.hostcache},
             {"autologin", &config.autologin},
             {"chwd", &config.chwd},
             {"carry_network", &config.carry_network},
             {"os_prober", &config.os_prober},
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
             {"server_profile", &config.server_profile},
             {"net_profiles_path", &config.net_profiles_path},
             {"post_install", &config.post_install},
             {"hw_clock", &config.hw_clock},
         }) {
        if (auto err = parse_optional_string(doc, key, *out)) {
            return std::unexpected(std::move(*err));
        }
    }

    for (const auto& [key, out] : std::initializer_list<std::pair<const char*, std::vector<std::string>*>>{
             {"ssh_authorized_keys", &config.ssh_authorized_keys},
             {"server_extra_packages", &config.server_extra_packages},
             {"user_groups", &config.user_groups},
             {"netinstall_groups", &config.netinstall_groups},
         }) {
        if (auto err = parse_optional_string_array(doc, key, *out)) {
            return std::unexpected(std::move(*err));
        }
    }

    for (const auto& [key, out] : std::initializer_list<std::pair<const char*, std::vector<std::uint16_t>*>>{
             {"server_extra_tcp_ports", &config.server_extra_tcp_ports},
             {"server_extra_udp_ports", &config.server_extra_udp_ports},
         }) {
        if (auto err = parse_optional_port_array(doc, key, *out)) {
            return std::unexpected(std::move(*err));
        }
    }

    // our aliases
    for (const auto& [canonical, alias, field] : std::initializer_list<std::tuple<const char*, const char*, std::optional<std::string>*>>{
             {"xkbmap", "keymap", &config.xkbmap},
             {"user_name", "username", &config.user_name},
             {"user_pass", "user_password", &config.user_pass},
             {"root_pass", "root_password", &config.root_pass},
         }) {
        if (auto err = apply_string_alias(doc, canonical, alias, *field)) {
            return std::unexpected(std::move(*err));
        }
    }

    // typed enums
    if (config.fs_name && !std::ranges::contains(kValidFilesystems, *config.fs_name)) {
        return std::unexpected(fmt::format(FMT_COMPILE("'fs_name' must be one of {}, got '{}'"), fmt::join(kValidFilesystems, ", "), *config.fs_name));
    }
    if (config.bootloader && !gucc::bootloader::bootloader_from_string(*config.bootloader)) {
        return std::unexpected(fmt::format(FMT_COMPILE("unknown 'bootloader': '{}'"), *config.bootloader));
    }
    if (config.hw_clock && *config.hw_clock != "utc"sv && *config.hw_clock != "localtime"sv) {
        return std::unexpected(fmt::format(FMT_COMPILE("'hw_clock' must be 'utc' or 'localtime', got '{}'"), *config.hw_clock));
    }

    // NOTE(vnepogodin): deprecated notice remove later
    const bool server_mode_present = doc.HasMember("server_mode");
    if (config.server_profile.has_value()) {
        if (server_mode_present && !config.server_mode) {
            return std::unexpected("server profile must be used only with enabled server mode");
        }
        config.server_mode = true;
    } else if (config.server_mode) {
        spdlog::warn("'server_mode' is deprecated. maped to server_profile minimal");
        config.server_profile = "minimal";
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
            return std::unexpected("'subvolumes' must be a string ('default') or an array");
        }
    }

    // headless defaults
    // NOTE: may adjust into own config similarly to calamares
    if (config.headless_mode) {
        if (!config.hostname) {
            config.hostname = "cachyos";
        }
        if (!config.locale) {
            config.locale = "en_US.UTF-8";
        }
        if (!config.xkbmap) {
            config.xkbmap = "us";
        }
        if (!config.timezone) {
            config.timezone = "UTC";
        }
        if (!config.user_shell) {
            config.user_shell = "/bin/bash";
        }
        if (!config.kernel) {
            config.kernel = "linux-cachyos";
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
    const bool is_server    = config.server_profile.has_value();

    const bool desktop_ok = is_server
        ? (!config.desktop.has_value() || config.desktop->empty())
        : config.desktop.has_value();

    const auto is_present = std::initializer_list<std::pair<std::string_view, bool>>{
        {"'device'"sv, config.device.has_value()},
        {"'fs_name'"sv, config.fs_name.has_value()},
        {"'partitions'"sv, !needs_layout || !config.partitions.empty()},
        {"'user_name'"sv, config.user_name.has_value()},
        {"'user_pass'"sv, config.user_pass.has_value()},
        {"'root_pass'"sv, config.root_pass.has_value()},
        {is_server ? "'desktop' cannot be used with server profile"sv : "'desktop'"sv, desktop_ok},
        {"'ssh_authorized_keys' required for server profile"sv, !is_server || !config.ssh_authorized_keys.empty()},
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

    // server related mapings
    inputs.ctx.server_profile         = cfg.server_profile.value_or("");
    inputs.ctx.ssh_authorized_keys    = cfg.ssh_authorized_keys;
    inputs.ctx.server_extra_packages  = cfg.server_extra_packages;
    inputs.ctx.server_extra_tcp_ports = cfg.server_extra_tcp_ports;
    inputs.ctx.server_extra_udp_ports = cfg.server_extra_udp_ports;
    if (cfg.net_profiles_path) {
        inputs.ctx.net_profiles_user_path = *cfg.net_profiles_path;
    }

    // fetch and set server profile
    if (auto res = cachyos::installer::init_server_profile(inputs.ctx); !res) {
        return std::unexpected(std::move(res).error());
    }

    // opts
    inputs.ctx.install_chwd_profiles = cfg.chwd;
    inputs.ctx.carry_live_network    = cfg.carry_network;
    inputs.ctx.disable_os_prober     = !cfg.os_prober;
    inputs.ctx.netinstall_groups     = cfg.netinstall_groups;

    if (cfg.xkbmap) {
        inputs.ctx.keymap = *cfg.xkbmap;
    }

    inputs.sys.hostname = cfg.hostname.value_or("");
    inputs.sys.locale   = cfg.locale.value_or("");
    inputs.sys.xkbmap   = cfg.xkbmap.value_or("");
    inputs.sys.keymap   = cfg.xkbmap.value_or("");
    inputs.sys.timezone = cfg.timezone.value_or("");
    if (cfg.hw_clock) {
        inputs.sys.hw_clock = (*cfg.hw_clock == "localtime"sv)
            ? SystemSettings::HwClock::Localtime
            : SystemSettings::HwClock::UTC;
    }

    inputs.user.username  = cfg.user_name.value_or("");
    inputs.user.password  = cfg.user_pass.value_or("");
    inputs.user.shell     = cfg.user_shell.value_or("");
    inputs.user.groups    = cfg.user_groups;
    inputs.user.autologin = cfg.autologin;

    inputs.root_password = cfg.root_pass.value_or("");

    return inputs;
}

}  // namespace cachyos::installer
