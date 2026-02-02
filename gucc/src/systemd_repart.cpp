#include "gucc/systemd_repart.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"

#include <algorithm>    // for ranges
#include <filesystem>   // for path, create_directories
#include <ranges>       // for ranges::*
#include <string_view>  // for string_view

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace fs = std::filesystem;

namespace gucc::disk {

auto encrypt_mode_to_string(RepartEncryptMode mode) noexcept -> std::string_view {
    switch (mode) {
    case RepartEncryptMode::Off:
        return "off"sv;
    case RepartEncryptMode::KeyFile:
        return "key-file"sv;
    case RepartEncryptMode::Tpm2:
        return "tpm2"sv;
    case RepartEncryptMode::KeyFilePlusTpm2:
        return "key-file+tpm2"sv;
    }
    return "off"sv;
}

auto empty_mode_to_string(RepartEmptyMode mode) noexcept -> std::string_view {
    switch (mode) {
    case RepartEmptyMode::Refuse:
        return "refuse"sv;
    case RepartEmptyMode::Allow:
        return "allow"sv;
    case RepartEmptyMode::Require:
        return "require"sv;
    case RepartEmptyMode::Force:
        return "force"sv;
    case RepartEmptyMode::Create:
        return "create"sv;
    }
    return "refuse"sv;
}

auto get_repart_type(fs::FilesystemType fs_type, std::string_view mountpoint) noexcept -> std::string_view {
    // Handle swap first
    if (fs_type == fs::FilesystemType::LinuxSwap) {
        return "swap"sv;
    }

    // Handle EFI/boot partitions
    if (fs_type == fs::FilesystemType::Vfat) {
        // NOTE: ignore xbootldr for /boot due to some issues with systemd-boot&fstab
        if (mountpoint == "/boot"sv || mountpoint == "/boot/efi"sv || mountpoint == "/efi"sv) {
            return "esp"sv;
        }
    }

    // Handle based on mountpoint
    if (mountpoint == "/"sv || mountpoint.empty()) {
        return "root"sv;
    }
    if (mountpoint == "/home"sv) {
        return "home"sv;
    }
    if (mountpoint == "/srv"sv) {
        return "srv"sv;
    }
    if (mountpoint == "/var"sv) {
        return "var"sv;
    }
    if (mountpoint == "/var/tmp"sv || mountpoint == "/tmp"sv) {
        return "tmp"sv;
    }
    if (mountpoint == "/usr"sv) {
        return "usr"sv;
    }
    return "linux-generic"sv;
}

auto generate_repart_config_content(const RepartPartitionConfig& config) noexcept -> std::string {
    std::string content = "[Partition]\n";

    // Type is required
    content += fmt::format(FMT_COMPILE("Type={}\n"), config.type);

    // Optional label
    if (config.label.has_value()) {
        content += fmt::format(FMT_COMPILE("Label={}\n"), *config.label);
    }

    // Size constraints
    if (config.size_min.has_value()) {
        content += fmt::format(FMT_COMPILE("SizeMinBytes={}\n"), *config.size_min);
    }
    if (config.size_max.has_value()) {
        content += fmt::format(FMT_COMPILE("SizeMaxBytes={}\n"), *config.size_max);
    }

    // Format specification
    if (config.format.has_value()) {
        content += fmt::format(FMT_COMPILE("Format={}\n"), *config.format);
    }

    // Encryption
    if (config.encrypt != RepartEncryptMode::Off) {
        content += fmt::format(FMT_COMPILE("Encrypt={}\n"), encrypt_mode_to_string(config.encrypt));
    }

    // Priority
    if (config.priority.has_value()) {
        content += fmt::format(FMT_COMPILE("Priority={}\n"), *config.priority);
    }

    // Weight
    if (config.weight.has_value()) {
        content += fmt::format(FMT_COMPILE("Weight={}\n"), *config.weight);
    }

    // Subvolumes for btrfs
    if (!config.subvolumes.empty()) {
        std::string subvols_str;
        for (const auto& subvol : config.subvolumes) {
            if (!subvols_str.empty()) {
                subvols_str += " ";
            }
            subvols_str += subvol;
        }
        content += fmt::format(FMT_COMPILE("Subvolumes={}\n"), subvols_str);
    }

    // Default subvolume
    if (config.default_subvolume.has_value()) {
        content += fmt::format(FMT_COMPILE("DefaultSubvolume={}\n"), *config.default_subvolume);
    }

    // Grow filesystem
    if (config.grow_filesystem) {
        content += "GrowFileSystem=yes\n";
    }

    return content;
}

auto write_repart_config(std::string_view dir_path, std::string_view filename, const RepartPartitionConfig& config) noexcept -> bool {
    const auto dir = ::fs::path{dir_path};
    if (!::fs::exists(dir)) {
        std::error_code ec;
        if (!::fs::create_directories(dir, ec)) {
            spdlog::error("Failed to create directory '{}': {}", dir_path, ec.message());
            return false;
        }
    }

    const auto file_path = dir / filename;
    const auto content   = generate_repart_config_content(config);
    if (!file_utils::create_file_for_overwrite(file_path.string(), content)) {
        spdlog::error("Failed to write repart config to '{}'", file_path.string());
        return false;
    }

    spdlog::debug("Wrote repart config to '{}'", file_path.string());
    return true;
}

auto write_repart_configs(std::string_view dir_path, const std::vector<RepartPartitionConfig>& partitions) noexcept -> bool {
    std::size_t index = 0;
    for (const auto& partition : partitions) {
        const auto filename = fmt::format(FMT_COMPILE("{:02d}-{}.conf"), index * 10, partition.type);
        if (!write_repart_config(dir_path, filename, partition)) {
            return false;
        }

        ++index;
    }

    spdlog::info("Wrote {} repart config files to '{}'", partitions.size(), dir_path);
    return true;
}

auto run_systemd_repart(const RepartConfig& config) noexcept -> bool {
    // Build the command
    std::string cmd = "systemd-repart";

    // Dry run option
    cmd += fmt::format(FMT_COMPILE(" --dry-run={}"), config.dry_run ? "yes" : "no");

    // Empty mode
    cmd += fmt::format(FMT_COMPILE(" --empty={}"), empty_mode_to_string(config.empty_mode));

    // Definitions directory
    if (!config.definitions_dir.empty()) {
        cmd += fmt::format(FMT_COMPILE(" --definitions={}"), config.definitions_dir);
    }

    // Root directory
    if (config.root_dir.has_value()) {
        cmd += fmt::format(FMT_COMPILE(" --root={}"), *config.root_dir);
    }

    // Key file for LUKS
    if (config.key_file.has_value()) {
        cmd += fmt::format(FMT_COMPILE(" --key-file={}"), *config.key_file);
    }

    // TPM2 device
    if (config.tpm2_device.has_value()) {
        cmd += fmt::format(FMT_COMPILE(" --tpm2-device={}"), *config.tpm2_device);
    }

    // TPM2 PCRs
    if (config.tpm2_pcrs.has_value()) {
        cmd += fmt::format(FMT_COMPILE(" --tpm2-pcrs={}"), *config.tpm2_pcrs);
    }

    // Target device
    cmd += fmt::format(FMT_COMPILE(" {}"), config.target_device);

    // Execute the command
    spdlog::info("Executing: {}", cmd);
    if (!utils::exec_checked(cmd)) {
        spdlog::error("systemd-repart command failed: {}", cmd);
        return false;
    }

    return true;
}

auto preview_systemd_repart(const RepartConfig& config) noexcept -> std::string {
    // Always dry run for preview
    std::string cmd = "systemd-repart --pretty=yes --dry-run=yes";

    // Empty mode
    cmd += fmt::format(FMT_COMPILE(" --empty={}"), empty_mode_to_string(config.empty_mode));

    // Definitions directory
    if (!config.definitions_dir.empty()) {
        cmd += fmt::format(FMT_COMPILE(" --definitions={}"), config.definitions_dir);
    }

    // Root directory
    if (config.root_dir.has_value()) {
        cmd += fmt::format(FMT_COMPILE(" --root={}"), *config.root_dir);
    }

    // Target device
    cmd += fmt::format(FMT_COMPILE(" {}"), config.target_device);

    // Execute and capture output
    spdlog::debug("Executing preview: {}", cmd);
    return utils::exec(cmd);
}

auto convert_partition_to_repart(const fs::Partition& partition) noexcept -> RepartPartitionConfig {
    RepartPartitionConfig config{};

    // Determine filesystem type
    const auto fs_type = fs::string_to_filesystem_type(partition.fstype);

    // Set type and format
    config.type = get_repart_type(fs_type, partition.mountpoint);
    if (fs_type != fs::FilesystemType::Unknown) {
        config.format = std::string{fs::filesystem_type_to_string(fs_type)};
    }

    // Set size if specified
    if (!partition.size.empty()) {
        config.size_min = partition.size;
        // For fixed-size partitions, set both min and max
        // If size ends with no unit or has specific size, make it fixed
        config.size_max = partition.size;
    }

    // Handle LUKS encryption
    if (partition.luks_mapper_name.has_value()) {
        // Default to key-file encryption when LUKS is specified
        config.encrypt = RepartEncryptMode::KeyFile;
    }

    // Set label based on mountpoint
    if (!partition.mountpoint.empty() && partition.mountpoint != "/"sv) {
        std::string label = partition.mountpoint;
        if (label.starts_with('/')) {
            label = label.substr(1);
        }
        std::ranges::replace(label, '/', '-');
        if (!label.empty()) {
            config.label = label;
        }
    }

    // Handle btrfs subvolumes
    if (partition.subvolume.has_value() && fs_type == fs::FilesystemType::Btrfs) {
        config.subvolumes.push_back(*partition.subvolume);
    }

    return config;
}

auto convert_partitions_to_repart(const std::vector<fs::Partition>& partitions) noexcept -> std::vector<RepartPartitionConfig> {
    std::vector<RepartPartitionConfig> result;
    result.reserve(partitions.size());

    for (const auto& partition : partitions) {
        // Subvolumes should be combined with parent partition
        if (partition.subvolume.has_value()) {
            auto it = std::ranges::find_if(result, [&](const RepartPartitionConfig& cfg) {
                return cfg.format.has_value() && *cfg.format == "btrfs"sv;
            });

            // Add subvolume to existing btrfs partition config
            if (it != std::ranges::end(result)) {
                it->subvolumes.push_back(*partition.subvolume);
                continue;
            }
        }

        result.push_back(convert_partition_to_repart(partition));
    }

    return result;
}

}  // namespace gucc::disk
