#include "gucc/partitioning.hpp"
#include "gucc/block_devices.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/ranges_utils.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/system_query.hpp"

#include <algorithm>    // for stable_partition, any_of, count, count_if, find
#include <array>        // for array
#include <cstdint>      // for uint32_t, uint64_t
#include <functional>   // for not_fn
#include <initializer_list>  // for initializer_list
#include <optional>     // for optional
#include <ranges>       // for ranges::*
#include <string>       // for string, to_string
#include <string_view>  // for string_view
#include <utility>      // for pair
#include <vector>       // for vector

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace {

struct PreviewRow final {
    std::string_view device;
    std::string_view size;
    std::string_view fstype;
    std::string_view mountpoint;
    std::string mount_opts;
};

constexpr auto to_preview_row(const gucc::fs::Partition& part) noexcept -> PreviewRow {
    const auto opts_str = part.mount_opts.empty() ? "defaults"sv : std::string_view{part.mount_opts};
    return PreviewRow{
        .device     = part.device,
        .size       = part.size.empty() ? "<remaining>"sv : std::string_view{part.size},
        .fstype     = part.fstype,
        .mountpoint = part.mountpoint.empty() ? "-"sv : std::string_view{part.mountpoint},
        .mount_opts = (opts_str.size() > 30) ? fmt::format(FMT_COMPILE("{}..."), opts_str.substr(0, 27)) : std::string{opts_str},
    };
}

constexpr auto is_fill_size(std::string_view size) noexcept -> bool {
    const auto trimmed = gucc::utils::trim(size);
    return trimmed.empty() || trimmed == "100%"sv;
}

// single sfdisk command line
constexpr auto to_sfdisk_line(const gucc::fs::Partition& part, bool is_efi) noexcept -> std::string {
    const auto fs_type = gucc::fs::string_to_filesystem_type(part.fstype);
    static constexpr auto SD_GPT_ROOT_X86_64 = "4f68bce3-e8cd-4db1-96e7-fbcaf984b709"sv;

    // L - alias 'linux'. Linux
    // U - alias 'uefi'. EFI System partition
    auto line = fmt::format(FMT_COMPILE("type={}"), (part.is_root() && is_efi) ?
        SD_GPT_ROOT_X86_64 : gucc::fs::get_sfdisk_type_alias(fs_type));

    // The field size= support '+' and '-' in the same way as Unnamed-fields
    // format. The default value of size indicates "as much as possible";
    // i.e., until the next partition or end-of-device. A numerical argument is
    // by default interpreted as a number of sectors, however if the size
    // is followed by one of the multiplicative suffixes (KiB, MiB, GiB,
    // TiB, PiB, EiB, ZiB and YiB) then the number is interpreted
    // as the size of the partition in bytes and it is then aligned
    // according to the device I/O limits. A '+' can be used instead of a
    // number to enlarge the partition as much as possible. Note '+' is
    // equivalent to the default behaviour for a new partition; existing
    // partitions will be resized as required.
    if (!part.size.empty()) {
        line += fmt::format(FMT_COMPILE(",size={}"), part.size);
    }

    // set boot flag
    if (fs_type == gucc::fs::FilesystemType::Vfat) {
        // bootable is specified as [*|-], with as default not-bootable.
        // The value of this field is irrelevant for Linux
        // - when Linux runs it has been booted already
        // - but it might play a role for certain boot loaders and for other operating systems.
        line += ",bootable"s;
    }

    line += "\n"s;
    return line;
}

// lsblk START unit, whatever the physical sector size
constexpr std::uint64_t kSectorBytes{512};

auto parted_argv(std::string_view device, std::initializer_list<std::string> args) -> std::vector<std::string> {
    std::vector<std::string> argv{"/usr/bin/parted"s, "-s"s, "-a"s, "optimal"s, std::string{device}};
    argv.append_range(args);
    return argv;
}

// parted takes byte offsets only with an explicit unit
auto bytes_arg(std::uint64_t bytes) -> std::string {
    return fmt::format(FMT_COMPILE("{}B"), bytes);
}

// first number following @p label in a tool report
constexpr auto number_after(std::string_view text, std::string_view label) noexcept -> std::optional<std::uint64_t> {
    const auto at = text.find(label);
    if (at == std::string_view::npos) {
        return std::nullopt;
    }
    const auto rest  = text.substr(at + label.size());
    const auto first = rest.find_first_of("0123456789"sv);
    if (first == std::string_view::npos) {
        return std::nullopt;
    }
    return gucc::utils::parse_uint<std::uint64_t>(rest.substr(first));
}

auto resize_filesystem(std::string_view partition, std::string_view fstype, std::uint64_t new_size_bytes) noexcept -> gucc::Result<void> {
    using gucc::fs::FilesystemType;
    const auto part = std::string{partition};
    if (gucc::fs::string_to_filesystem_type(fstype) == FilesystemType::Ext4) {
        // resize2fs refuses a filesystem that was not just checked; exit 1 means errors were fixed
        if (!gucc::utils::exec_checked(std::vector{"/usr/bin/e2fsck"s, "-f"s, "-y"s, part})) {
            spdlog::warn("e2fsck reported problems on {}", partition);
        }
        // rounding down keeps the filesystem inside the partition
        return gucc::utils::exec_or_error(std::vector{"/usr/bin/resize2fs"s, part, fmt::format(FMT_COMPILE("{}K"), new_size_bytes / 1024U)}, "resize filesystem"sv);
    }
    if (fstype == "ntfs"sv) {
        // ntfsresize asks for confirmation on stdin
        return gucc::utils::exec_or_error_with_input(std::vector{"/usr/bin/ntfsresize"s, "--force"s, "--no-progress-bar"s, "--size"s, std::to_string(new_size_bytes), part}, "y\n"sv, "resize filesystem"sv);
    }
    return gucc::make_error(gucc::ErrorCode::Unsupported, FMT_COMPILE("cannot shrink '{}' on {}"), fstype, partition);
}

}  // namespace

namespace gucc::disk {

auto gen_sfdisk_command(const std::vector<fs::Partition>& partitions, bool is_efi) noexcept -> std::string {
    // NOTE: sfdisk allocates partitions in the order it reads them, so emission
    // order decides the resulting partition numbers. Callers assign the device
    // paths in declaration order (see generate_partition_schema_from_config),
    // so declaration order must be preserved here, otherwise the schema's
    // device paths stop matching the partitions actually created.

    // filter duplicates by device, keeping the first occurrence
    // NOTE: the predicate must be pure
    const auto is_first_occurrence = [&partitions](const auto& indexed) {
        const auto& [index, part] = indexed;
        return std::ranges::find(partitions, part.device, &fs::Partition::device) == utils::index_viewable_range(partitions, index);
    };

    auto partitions_filtered = partitions
        | std::ranges::views::enumerate
        | std::ranges::views::filter(is_first_occurrence)
        | std::ranges::views::values
        | std::ranges::to<std::vector<fs::Partition>>();

    // drop full size percentage
    for (auto& part : partitions_filtered) {
        if (is_fill_size(part.size)) {
            part.size.clear();
        }
    }

    // empty sized parts must be at the end, they take whatever space is left.
    // stable, to leave declaration order otherwise untouched
    std::ranges::stable_partition(partitions_filtered, std::not_fn(&std::string::empty), &fs::Partition::size);

    // sfdisk does not create partition table without partitions by default. The lines with partitions are expected in the script by default.
    auto sfdisk_commands = fmt::format(FMT_COMPILE("label: {}\n"), is_efi ? "gpt"sv : "dos"sv);
    sfdisk_commands += partitions_filtered
        | std::ranges::views::transform([&is_efi](fs::Partition part) -> std::string { return to_sfdisk_line(part, is_efi); })
        | std::ranges::views::join
        | std::ranges::to<std::string>();
    return sfdisk_commands;
}

auto run_sfdisk_part(std::string_view commands, std::string_view device) noexcept -> Result<void> {
    if (auto res = utils::exec_or_error_with_input({"/usr/bin/sfdisk"s, "-w"s, "always"s, std::string{device}}, commands, "partition disk"sv); !res) {
        return res;
    }
    // let the new partition nodes/links appear before anyone formats them
    utils::settle_devices();
    return {};
}

auto erase_disk(std::string_view device) noexcept -> Result<void> {
    const auto dev = std::string{device};
    const std::array steps{
        // 1. zero the first sector
        std::vector{"/usr/bin/dd"s, "if=/dev/zero"s, fmt::format(FMT_COMPILE("of={}"), device), "bs=512"s, "count=1"s},
        // 2. drop every filesystem/raid/partition-table signature
        std::vector{"/usr/bin/wipefs"s, "-af"s, dev},
        // 3. destroy GPT and MBR data structures
        std::vector{"/usr/bin/sgdisk"s, "-Zo"s, dev},
    };
    for (const auto& argv : steps) {
        if (auto res = utils::exec_or_error(argv, "erase disk"sv); !res) {
            return res;
        }
    }
    return {};
}

auto secure_wipe(std::string_view device) noexcept -> Result<void> {
    return utils::exec_or_error(std::vector{"/usr/bin/wipe"s, "-Ifre"s, std::string{device}}, "wipe device"sv);
}

auto make_clean_partschema(std::string_view device, const std::vector<fs::Partition>& partitions, bool is_efi) noexcept -> Result<void> {
    // clear disk
    if (auto res = erase_disk(device); !res) {
        return res;
    }
    // apply schema
    return run_sfdisk_part(gen_sfdisk_command(partitions, is_efi), device);
}

auto generate_partition_schema_from_config(std::string_view device, const fs::DefaultPartitionSchemaConfig& config, bool is_efi) noexcept -> std::vector<fs::Partition> {
    // NOTE(vnepogodin): function partition schema assumes we will use whole drive, and ignores ZFS or BTRFS
    std::vector<fs::Partition> partitions{};

    // Get root mount opts from config or use defaults
    const auto& root_mount_opts = config.root_mount_opts.value_or(
        fs::get_default_mount_opts(config.root_fs_type, config.is_ssd));

    // currently partition uuid doesn't matter, it will be assigned much after during FS partitioning
    const auto add_partition = [&](std::string fstype, std::string mountpoint, std::string size, std::string mount_opts) {
        partitions.emplace_back(fs::Partition{
            .fstype     = std::move(fstype),
            .mountpoint = std::move(mountpoint),
            .uuid_str   = {},
            .device     = insert_partition_number(device, static_cast<std::uint32_t>(partitions.size() + 1)),
            .size       = std::move(size),
            .mount_opts = std::move(mount_opts)});
    };

    // For UEFI: create EFI partition first
    if (is_efi) {
        add_partition("vfat"s, config.boot_mountpoint, config.efi_partition_size, fs::get_default_mount_opts(fs::FilesystemType::Vfat, config.is_ssd));
    } else if (config.boot_partition_size) {
        // For BIOS with separate boot partition
        add_partition("ext4"s, config.boot_mountpoint, *config.boot_partition_size, fs::get_default_mount_opts(fs::FilesystemType::Ext4, config.is_ssd));
    }
    // Create swap partition if configured
    if (config.swap_partition_size) {
        add_partition("linuxswap"s, {}, *config.swap_partition_size, "defaults"s);
    }
    // root takes the remaining space
    add_partition(std::string{fs::filesystem_type_to_string(config.root_fs_type)}, "/"s, {}, root_mount_opts);

    return partitions;
}

auto validate_partition_schema(const std::vector<fs::Partition>& partitions, std::string_view device, bool is_efi) noexcept -> fs::PartitionSchemaValidation {
    fs::PartitionSchemaValidation result{};
    result.is_valid = true;

    // what is that device?? where did you lose it
    if (device.empty()) {
        result.warnings.emplace_back("No target device specified");
    }

    if (partitions.empty()) {
        result.is_valid = false;
        result.errors.emplace_back("Partition schema is empty");
        return result;
    }

    // Check for root partition
    const bool has_root = std::ranges::any_of(partitions, &fs::Partition::is_root);
    if (!has_root) {
        result.is_valid = false;
        result.errors.emplace_back("No root (/) partition defined");
    }

    // every vfat partition gets the ESP type
    const auto efi_count = std::ranges::count(partitions, fs::FilesystemType::Vfat,
        [](const fs::Partition& part) { return fs::string_to_filesystem_type(part.fstype); });
    if (is_efi && efi_count == 0) {
        result.is_valid = false;
        result.errors.emplace_back("UEFI system requires an EFI partition (vfat/fat32)");
    } else if (is_efi && efi_count > 1) {
        result.is_valid = false;
        result.errors.push_back(fmt::format(FMT_COMPILE("UEFI system requires exactly one EFI partition (vfat/fat32), found {}"), efi_count));
    }

    // Invalid partition sizes. subvols never carry a size
    const auto empty_size_count = std::ranges::count_if(partitions,
        [](const fs::Partition& part) { return part.size.empty() && !part.subvolume; });
    if (empty_size_count > 1) {
        result.warnings.emplace_back("Multiple partitions without specified size - only the last will use remaining space");
    }

    return result;
}

auto preview_partition_schema(const std::vector<fs::Partition>& partitions, std::string_view device, bool is_efi) noexcept -> std::string {
    std::string preview{};

    // Header
    preview += fmt::format(FMT_COMPILE("=== Partition Schema for {} ===\n"), device);
    preview += fmt::format(FMT_COMPILE("Mode: {}\n\n"), is_efi ? "UEFI (GPT)" : "BIOS (MBR)");

    // Partition table header
    preview += fmt::format(FMT_COMPILE("{:<20} {:<12} {:<12} {:<15} {}\n"),
        "Device", "Size", "Filesystem", "Mount Point", "Options");
    preview += std::string(80, '-') + "\n";

    // subvols get their own table
    constexpr auto is_subvolume_entry = [](const fs::Partition& part) { return part.subvolume.has_value(); };
    constexpr auto is_partition_entry = std::not_fn(is_subvolume_entry);

    auto preview_rows = partitions | std::ranges::views::filter(is_partition_entry) | std::ranges::views::transform(to_preview_row);
    for (const auto& row : preview_rows) {
        preview += fmt::format(FMT_COMPILE("{:<20} {:<12} {:<12} {:<15} {}\n"),
            row.device, row.size, row.fstype, row.mountpoint, row.mount_opts);
    }

    // pretty-print the subvols
    const bool has_subvols = std::ranges::any_of(partitions, &std::optional<std::string>::has_value, &fs::Partition::subvolume);
    if (has_subvols) {
        preview += "\nBtrfs Subvolumes:\n";
        preview += fmt::format(FMT_COMPILE("{:<20} {:<15}\n"), "Subvolume", "Mount Point");
        preview += std::string(40, '-') + "\n";

        auto subvols = partitions | std::ranges::views::filter(is_subvolume_entry)
            | std::ranges::views::transform([](const fs::Partition& part) {
                  return std::pair<std::string_view, std::string_view>{*part.subvolume, part.mountpoint};
              });
        for (const auto& [subvolume, mountpoint] : subvols) {
            preview += fmt::format(FMT_COMPILE("{:<20} {:<15}\n"), subvolume, mountpoint);
        }
    }

    // insert informative stuff that we might want at hand
    const auto& validation = validate_partition_schema(partitions, device, is_efi);
    if (!validation.errors.empty() || !validation.warnings.empty()) {
        preview += "\n";
        for (const auto& error : validation.errors) {
            preview += fmt::format(FMT_COMPILE("[ERROR] {}\n"), error);
        }
        for (const auto& warning : validation.warnings) {
            preview += fmt::format(FMT_COMPILE("[WARNING] {}\n"), warning);
        }
    }

    preview += "\n--- sfdisk commands ---\n";
    preview += gen_sfdisk_command(partitions, is_efi);

    return preview;
}

auto create_partition_table(std::string_view device, std::string_view table_type) noexcept -> Result<void> {
    return utils::exec_or_error(parted_argv(device, {"mklabel"s, std::string{table_type}}), "create partition table"sv);
}

auto create_partition(std::string_view device, std::uint64_t start_bytes, std::uint64_t end_bytes) noexcept -> Result<std::string> {
    if (end_bytes <= start_bytes) {
        return make_error(ErrorCode::InvalidArgument, FMT_COMPILE("empty partition range [{}, {}] on {}"), start_bytes, end_bytes, device);
    }
    // the partition type is mandatory on msdos and the name on gpt
    if (auto res = utils::exec_or_error(parted_argv(device, {"unit"s, "B"s, "mkpart"s, "primary"s, bytes_arg(start_bytes), bytes_arg(end_bytes)}), "create partition"sv); !res) {
        return std::unexpected(std::move(res).error());
    }
    utils::settle_devices();

    // the range was free, so whatever starts inside it is the new partition
    const auto devices = list_block_devices();
    if (!devices) {
        return make_error(ErrorCode::SubprocessFailed, FMT_COMPILE("cannot list partitions of {}"), device);
    }
    const auto in_range = [device, start_bytes, end_bytes](const BlockDevice& dev) {
        if (dev.type != "part"sv || dev.pkname != device || !dev.start) {
            return false;
        }
        const auto start = *dev.start * kSectorBytes;
        return start >= start_bytes && start <= end_bytes;
    };
    const auto* created = utils::find_if_ptr(*devices, in_range);
    if (created == nullptr) {
        return make_error(ErrorCode::NotFound, FMT_COMPILE("no new partition between {} and {} on {}"), start_bytes, end_bytes, device);
    }
    return created->name;
}

auto delete_partition(std::string_view device, std::uint32_t number) noexcept -> Result<void> {
    return utils::exec_or_error(parted_argv(device, {"rm"s, std::to_string(number)}), "delete partition"sv);
}

auto set_partition_flag(std::string_view device, std::uint32_t number, std::string_view flag, bool state) noexcept -> Result<void> {
    return utils::exec_or_error(parted_argv(device, {"set"s, std::to_string(number), std::string{flag}, state ? "on"s : "off"s}), "set partition flag"sv);
}

auto shrink_partition(std::string_view device, std::uint32_t number, std::string_view fstype,
    std::uint64_t new_size_bytes, std::uint64_t new_end_bytes) noexcept -> Result<void> {
    // filesystem first, so the table never cuts live data
    if (auto res = resize_filesystem(insert_partition_number(device, number), fstype, new_size_bytes); !res) {
        return res;
    }
    return utils::exec_or_error(parted_argv(device, {"unit"s, "B"s, "resizepart"s, std::to_string(number), bytes_arg(new_end_bytes)}), "resize partition"sv);
}

auto filesystem_used_bytes(std::string_view partition, std::string_view fstype) noexcept -> Result<std::uint64_t> {
    const auto part = std::string{partition};
    if (fs::string_to_filesystem_type(fstype) == fs::FilesystemType::Ext4) {
        const auto out    = utils::query_output(std::vector{"/usr/bin/dumpe2fs"s, "-h"s, part}).value_or(""s);
        const auto blocks = number_after(out, "Block count:"sv);
        const auto free   = number_after(out, "Free blocks:"sv);
        const auto size   = number_after(out, "Block size:"sv);
        if (!blocks || !free || !size || *free > *blocks) {
            return make_error(ErrorCode::ParseError, FMT_COMPILE("cannot read ext4 usage of {}"), partition);
        }
        return (*blocks - *free) * *size;
    }
    if (fstype == "ntfs"sv) {
        const auto out = utils::query_output(std::vector{"/usr/bin/ntfsresize"s, "--info"s, "--force"s, "--no-progress-bar"s, part}).value_or(""s);
        if (const auto min = number_after(out, "You might resize at"sv)) {
            return *min;
        }
        return make_error(ErrorCode::ParseError, FMT_COMPILE("cannot read ntfs minimum size of {}"), partition);
    }
    return make_error(ErrorCode::Unsupported, FMT_COMPILE("no usage probe for '{}' on {}"), fstype, partition);
}

auto set_filesystem_label(std::string_view partition, std::string_view fstype, std::string_view label) noexcept -> Result<void> {
    using fs::FilesystemType;
    const auto part = std::string{partition};
    const auto name = std::string{label};
    std::vector<std::string> argv;
    switch (fs::string_to_filesystem_type(fstype)) {
    case FilesystemType::Ext4:
        argv = {"/usr/bin/e2label"s, part, name};
        break;
    case FilesystemType::Btrfs:
        argv = {"/usr/bin/btrfs"s, "filesystem"s, "label"s, part, name};
        break;
    case FilesystemType::Xfs:
        argv = {"/usr/bin/xfs_admin"s, "-L"s, name, part};
        break;
    case FilesystemType::Vfat:
        argv = {"/usr/bin/fatlabel"s, part, name};
        break;
    default:
        if (fstype != "ntfs"sv) {
            return make_error(ErrorCode::Unsupported, FMT_COMPILE("no label tool for '{}' on {}"), fstype, partition);
        }
        argv = {"/usr/bin/ntfslabel"s, part, name};
        break;
    }
    return utils::exec_or_error(argv, "set filesystem label"sv);
}

}  // namespace gucc::disk
