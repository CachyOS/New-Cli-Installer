#include "gucc/systemd_repart.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/ranges_utils.hpp"
#include "gucc/string_utils.hpp"

#include <cstdint>  // for uint8_t

#include <algorithm>    // for replace, find
#include <filesystem>   // for path
#include <ranges>       // for ranges::*
#include <string_view>  // for string_view

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace fs = std::filesystem;

namespace {

enum class RepartInvocation : std::uint8_t {
    Apply,
    Preview,
};

auto build_repart_argv(const gucc::disk::RepartConfig& config, RepartInvocation invocation) noexcept -> std::vector<std::string> {
    const bool preview = invocation == RepartInvocation::Preview;

    std::vector<std::string> args{"/usr/bin/systemd-repart"s};
    if (preview) {
        args.emplace_back("--pretty=yes"sv);
    }
    args.push_back(fmt::format(FMT_COMPILE("--dry-run={}"), (preview || config.dry_run) ? "yes"sv : "no"sv));
    args.push_back(fmt::format(FMT_COMPILE("--empty={}"), gucc::disk::empty_mode_to_string(config.empty_mode)));
    if (!config.definitions_dir.empty()) {
        args.push_back(fmt::format(FMT_COMPILE("--definitions={}"), config.definitions_dir));
    }
    if (config.root_dir) {
        args.push_back(fmt::format(FMT_COMPILE("--root={}"), *config.root_dir));
    }
    if (!preview) {
        if (config.key_file) {
            args.push_back(fmt::format(FMT_COMPILE("--key-file={}"), *config.key_file));
        }
        if (config.tpm2_device) {
            args.push_back(fmt::format(FMT_COMPILE("--tpm2-device={}"), *config.tpm2_device));
        }
        if (config.tpm2_pcrs) {
            args.push_back(fmt::format(FMT_COMPILE("--tpm2-pcrs={}"), *config.tpm2_pcrs));
        }
    }
    args.push_back(config.target_device);
    return args;
}

}  // namespace

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
        content += fmt::format(FMT_COMPILE("Subvolumes={}\n"), utils::join(config.subvolumes, ' '));
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

auto write_repart_config(std::string_view dir_path, std::string_view filename, const RepartPartitionConfig& config) noexcept -> Result<void> {
    if (auto res = file_utils::create_directories(dir_path); !res) {
        return res;
    }

    const auto& file_path = (::fs::path{dir_path} / filename).string();
    if (auto res = file_utils::write_file(file_path, generate_repart_config_content(config)); !res) {
        return res;
    }

    spdlog::debug("Wrote repart config to '{}'", file_path);
    return {};
}

auto write_repart_configs(std::string_view dir_path, const std::vector<RepartPartitionConfig>& partitions) noexcept -> Result<void> {
    for (const auto& [index, partition] : partitions | std::ranges::views::enumerate) {
        const auto& filename = fmt::format(FMT_COMPILE("{:02d}-{}.conf"), index * 10, partition.type);
        if (auto res = write_repart_config(dir_path, filename, partition); !res) {
            return res;
        }
    }

    spdlog::info("Wrote {} repart config files to '{}'", partitions.size(), dir_path);
    return {};
}

auto run_systemd_repart(const RepartConfig& config) noexcept -> Result<void> {
    const auto argv = build_repart_argv(config, RepartInvocation::Apply);
    return utils::exec_or_error(argv, "repartition disk"sv);
}

auto preview_systemd_repart(const RepartConfig& config) noexcept -> std::string {
    return utils::query(build_repart_argv(config, RepartInvocation::Preview));
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
            if (auto* btrfs = utils::find_ptr(result, std::optional{"btrfs"s}, &RepartPartitionConfig::format)) {
                btrfs->subvolumes.push_back(*partition.subvolume);
                continue;
            }
        }

        result.push_back(convert_partition_to_repart(partition));
    }

    return result;
}

}  // namespace gucc::disk
