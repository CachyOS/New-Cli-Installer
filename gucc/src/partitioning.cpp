#include "gucc/partitioning.hpp"
#include "gucc/block_devices.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/system_query.hpp"

#include <algorithm>      // for sort, unique_copy, any_of, count_if
#include <array>          // for array
#include <charconv>       // for from_chars
#include <cstddef>        // for size_t
#include <optional>       // for optional
#include <ranges>         // for ranges::*
#include <string_view>    // for string_view
#include <unordered_set>  // for unordered_set

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace {

constexpr auto convert_fsname(std::string_view fsname) noexcept -> std::string_view {
    if (fsname == "fat16"sv || fsname == "fat32"sv) {
        return "vfat"sv;
    } else if (fsname == "linuxswap"sv) {
        return "swap"sv;
    }
    return fsname;
}

constexpr auto get_part_type_alias(std::string_view fsname) noexcept -> std::string_view {
    if (fsname == "vfat"sv) {
        return "U"sv;
    } else if (fsname == "swap"sv) {
        return "S"sv;
    }
    return "L"sv;
}

// Trims a trailing ';' and a trailing 'B' (parted's byte-unit suffix) plus
// surrounding whitespace/CR from a `parted -m` field.
constexpr auto strip_byte_suffix(std::string_view field) noexcept -> std::string_view {
    while (!field.empty() && (field.back() == ';' || field.back() == '\r' || field.back() == ' ')) {
        field.remove_suffix(1);
    }
    if (!field.empty() && field.back() == 'B') {
        field.remove_suffix(1);
    }
    return field;
}

[[nodiscard]] auto field_to_u64(std::string_view field) noexcept -> std::optional<std::uint64_t> {
    const auto digits = strip_byte_suffix(field);
    std::uint64_t value{};
    const auto* const begin = digits.data();
    const auto* const end   = digits.data() + digits.size();
    const auto [ptr, ec]    = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

// Names (e.g. "sda3") of the partitions that hang off `device` right now.
[[nodiscard]] auto partition_names_under(std::string_view device) noexcept
    -> std::unordered_set<std::string> {
    std::unordered_set<std::string> names;
    const auto parent  = std::string{gucc::disk::strip_device_prefix(device)};
    const auto devices = gucc::disk::list_block_devices();
    if (!devices) {
        return names;
    }
    for (const auto& dev : *devices) {
        if (dev.type == "part"sv && dev.pkname.has_value() && *dev.pkname == parent) {
            names.insert(dev.name);
        }
    }
    return names;
}

}  // namespace

namespace gucc::disk {

auto parse_free_regions(std::string_view parted_machine_output) noexcept -> std::vector<FreeRegion> {
    std::vector<FreeRegion> regions;

    std::size_t pos = 0;
    while (pos <= parted_machine_output.size()) {
        const auto nl   = parted_machine_output.find('\n', pos);
        const auto line = parted_machine_output.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos             = (nl == std::string_view::npos) ? parted_machine_output.size() + 1 : nl + 1;

        // A free-space record is `NUM:START B:END B:SIZE B:free;`
        std::array<std::string_view, 5> fields{};
        std::size_t fcount = 0;
        std::size_t fp     = 0;
        while (fp <= line.size() && fcount < fields.size()) {
            const auto colon = line.find(':', fp);
            fields[fcount++] = line.substr(fp, colon == std::string_view::npos ? std::string_view::npos : colon - fp);
            if (colon == std::string_view::npos) {
                break;
            }
            fp = colon + 1;
        }
        if (fcount < 5 || strip_byte_suffix(fields[4]) != "free"sv) {
            continue;
        }

        const auto start = field_to_u64(fields[1]);
        const auto end   = field_to_u64(fields[2]);
        const auto size  = field_to_u64(fields[3]);
        if (start && end && size) {
            regions.push_back(FreeRegion{.start_bytes = *start, .end_bytes = *end, .size_bytes = *size});
        }
    }
    return regions;
}

auto parse_device_partitions(std::string_view parted_machine_output) noexcept -> std::vector<PartitionLayout> {
    std::vector<PartitionLayout> parts;

    std::size_t pos = 0;
    while (pos <= parted_machine_output.size()) {
        const auto nl   = parted_machine_output.find('\n', pos);
        const auto line = parted_machine_output.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos             = (nl == std::string_view::npos) ? parted_machine_output.size() + 1 : nl + 1;

        // Partition records are `NUM:START B:END B:SIZE B:FSTYPE:NAME:FLAGS;`
        std::vector<std::string_view> f;
        std::size_t fp = 0;
        while (fp <= line.size()) {
            const auto colon = line.find(':', fp);
            f.push_back(line.substr(fp, colon == std::string_view::npos ? std::string_view::npos : colon - fp));
            if (colon == std::string_view::npos) {
                break;
            }
            fp = colon + 1;
        }
        if (f.size() < 7) {
            continue;
        }

        const auto number = field_to_u64(f[0]);
        const auto start  = field_to_u64(f[1]);
        const auto end    = field_to_u64(f[2]);
        const auto size   = field_to_u64(f[3]);
        if (!number || !start || !end || !size) {
            continue;
        }

        // Flags field carries a trailing ';' and possibly surrounding spaces.
        auto flags = f[6];
        while (!flags.empty() && (flags.back() == ';' || flags.back() == '\r' || flags.back() == ' ')) {
            flags.remove_suffix(1);
        }

        parts.push_back(PartitionLayout{
            .number      = static_cast<std::uint32_t>(*number),
            .start_bytes = *start,
            .end_bytes   = *end,
            .size_bytes  = *size,
            .fstype      = std::string{f[4]},
            .name        = std::string{f[5]},
            .flags       = std::string{flags},
        });
    }
    return parts;
}

auto build_resize_filesystem_command(std::string_view partition, std::string_view fstype, std::uint64_t new_size_bytes) noexcept -> std::string {
    if (fstype == "ext2"sv || fstype == "ext3"sv || fstype == "ext4"sv) {
        // resize2fs in KiB; rounding down keeps the filesystem inside the partition.
        return fmt::format(FMT_COMPILE("resize2fs '{}' {}K"), partition, new_size_bytes / 1024U);
    }
    if (fstype == "ntfs"sv) {
        return fmt::format(FMT_COMPILE("ntfsresize --force --size {} '{}'"), new_size_bytes, partition);
    }
    // xfs can't shrink; btrfs needs an online (mounted) resize handled elsewhere.
    return {};
}

auto build_resize_partition_command(std::string_view device, std::uint32_t number, std::uint64_t new_end_bytes) noexcept -> std::string {
    return fmt::format(FMT_COMPILE("parted -s -m '{}' unit B resizepart {} {}B"), device, number, new_end_bytes);
}

auto build_create_partition_command(std::string_view device, std::uint64_t start_bytes, std::uint64_t end_bytes, std::string_view parted_fs) noexcept -> std::string {
    if (parted_fs.empty()) {
        return fmt::format(FMT_COMPILE("parted -s -m -a optimal '{}' unit B mkpart primary {}B {}B"), device, start_bytes, end_bytes);
    }
    return fmt::format(FMT_COMPILE("parted -s -m -a optimal '{}' unit B mkpart primary {} {}B {}B"), device, parted_fs, start_bytes, end_bytes);
}

auto build_delete_partition_command(std::string_view device, std::uint32_t number) noexcept -> std::string {
    return fmt::format(FMT_COMPILE("parted -s -m '{}' rm {}"), device, number);
}

auto build_set_flag_command(std::string_view device, std::uint32_t number, std::string_view flag, bool state) noexcept -> std::string {
    return fmt::format(FMT_COMPILE("parted -s -m '{}' set {} {} {}"), device, number, flag, state ? "on" : "off");
}

auto build_create_table_command(std::string_view device, std::string_view table_type) noexcept -> std::string {
    return fmt::format(FMT_COMPILE("parted -s -m '{}' mklabel {}"), device, table_type);
}

auto list_device_partitions(std::string_view device) noexcept -> std::vector<PartitionLayout> {
    const auto cmd = fmt::format(FMT_COMPILE("parted -m -s '{}' unit B print 2>/dev/null"), device);
    return parse_device_partitions(utils::exec(cmd));
}

auto resize_filesystem(std::string_view partition, std::string_view fstype, std::uint64_t new_size_bytes) noexcept -> Result<void> {
    const auto cmd = build_resize_filesystem_command(partition, fstype, new_size_bytes);
    if (cmd.empty()) {
        return make_error(ErrorCode::InvalidArgument, fmt::format("filesystem '{}' cannot be resized", fstype));
    }
    // ext* requires a forced check before resize2fs will run.
    if (fstype.starts_with("ext"sv)) {
        utils::exec_checked(fmt::format(FMT_COMPILE("e2fsck -f -y '{}' &>>/tmp/cachyos-install.log"), partition));
    }
    if (!utils::exec_checked(fmt::format(FMT_COMPILE("{} &>>/tmp/cachyos-install.log"), cmd))) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("failed to resize filesystem on {}", partition));
    }
    return {};
}

auto resize_partition(std::string_view device, std::uint32_t number, std::uint64_t new_end_bytes) noexcept -> Result<void> {
    const auto cmd = build_resize_partition_command(device, number, new_end_bytes);
    if (!utils::exec_checked(fmt::format(FMT_COMPILE("echo Yes | {} &>>/tmp/cachyos-install.log"), cmd))) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("failed to resize partition {} on {}", number, device));
    }
    return {};
}

auto shrink_partition(std::string_view device, std::uint32_t number, std::string_view partition,
    std::string_view fstype, std::uint64_t new_size_bytes, std::uint64_t new_end_bytes) noexcept -> Result<void> {
    if (auto res = resize_filesystem(partition, fstype, new_size_bytes); !res) {
        return res;
    }
    return resize_partition(device, number, new_end_bytes);
}

auto filesystem_used_bytes(std::string_view partition, std::string_view fstype) noexcept -> Result<std::uint64_t> {
    const auto value_after = [](std::string_view text, std::string_view label) -> std::optional<std::uint64_t> {
        const auto at = text.find(label);
        if (at == std::string_view::npos) {
            return std::nullopt;
        }
        auto rest = text.substr(at + label.size());
        const auto first = rest.find_first_of("0123456789");
        if (first == std::string_view::npos) {
            return std::nullopt;
        }
        rest = rest.substr(first);
        const auto last = rest.find_first_not_of("0123456789");
        return field_to_u64(last == std::string_view::npos ? rest : rest.substr(0, last));
    };

    if (fstype.starts_with("ext"sv)) {
        const auto out = utils::exec(fmt::format(FMT_COMPILE("dumpe2fs -h '{}' 2>/dev/null"), partition));
        const auto count = value_after(out, "Block count:"sv);
        const auto freeb = value_after(out, "Free blocks:"sv);
        const auto bsize = value_after(out, "Block size:"sv);
        if (!count || !freeb || !bsize) {
            return make_error(ErrorCode::ParseError, fmt::format("could not read ext usage on {}", partition));
        }
        return (*count - *freeb) * *bsize;
    }
    if (fstype == "ntfs"sv) {
        const auto out = utils::exec(fmt::format(FMT_COMPILE("ntfsresize --info --force '{}' 2>/dev/null"), partition));
        if (const auto min = value_after(out, "You might resize at"sv)) {
            return *min;
        }
        return make_error(ErrorCode::ParseError, fmt::format("could not read ntfs minimum size on {}", partition));
    }
    return make_error(ErrorCode::InvalidArgument, fmt::format("no usage probe for filesystem '{}'", fstype));
}

auto delete_partition(std::string_view device, std::uint32_t number) noexcept -> Result<void> {
    if (!utils::exec_checked(fmt::format(FMT_COMPILE("{} &>>/tmp/cachyos-install.log"), build_delete_partition_command(device, number)))) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("failed to delete partition {} on {}", number, device));
    }
    return {};
}

auto format_partition(std::string_view partition, std::string_view fstype, std::string_view label) noexcept -> Result<void> {
    const auto mkfs = fs::get_mkfs_command(fs::string_to_filesystem_type(fstype));
    if (mkfs.empty()) {
        return make_error(ErrorCode::InvalidArgument, fmt::format("no mkfs command for filesystem '{}'", fstype));
    }
    auto cmd = fmt::format(FMT_COMPILE("{} '{}'"), mkfs, partition);
    if (!utils::exec_checked(fmt::format(FMT_COMPILE("{} &>>/tmp/cachyos-install.log"), cmd))) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("failed to format {} as {}", partition, fstype));
    }
    if (!label.empty()) {
        return set_filesystem_label(partition, fstype, label);
    }
    return {};
}

auto set_partition_flag(std::string_view device, std::uint32_t number, std::string_view flag, bool state) noexcept -> Result<void> {
    if (!utils::exec_checked(fmt::format(FMT_COMPILE("{} &>>/tmp/cachyos-install.log"), build_set_flag_command(device, number, flag, state)))) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("failed to set flag {} on partition {} of {}", flag, number, device));
    }
    return {};
}

auto set_filesystem_label(std::string_view partition, std::string_view fstype, std::string_view label) noexcept -> Result<void> {
    std::string cmd;
    if (fstype.starts_with("ext"sv)) {
        cmd = fmt::format(FMT_COMPILE("e2label '{}' '{}'"), partition, label);
    } else if (fstype == "btrfs"sv) {
        cmd = fmt::format(FMT_COMPILE("btrfs filesystem label '{}' '{}'"), partition, label);
    } else if (fstype == "xfs"sv) {
        cmd = fmt::format(FMT_COMPILE("xfs_admin -L '{}' '{}'"), label, partition);
    } else if (fstype == "vfat"sv || fstype == "fat32"sv || fstype == "fat16"sv) {
        cmd = fmt::format(FMT_COMPILE("fatlabel '{}' '{}'"), partition, label);
    } else if (fstype == "ntfs"sv) {
        cmd = fmt::format(FMT_COMPILE("ntfslabel '{}' '{}'"), partition, label);
    } else {
        return make_error(ErrorCode::InvalidArgument, fmt::format("no label tool for filesystem '{}'", fstype));
    }
    if (!utils::exec_checked(fmt::format(FMT_COMPILE("{} &>>/tmp/cachyos-install.log"), cmd))) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("failed to label {} as '{}'", partition, label));
    }
    return {};
}

auto create_partition_table(std::string_view device, std::string_view table_type) noexcept -> Result<void> {
    if (!utils::exec_checked(fmt::format(FMT_COMPILE("{} &>>/tmp/cachyos-install.log"), build_create_table_command(device, table_type)))) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("failed to create {} table on {}", table_type, device));
    }
    return {};
}

auto list_free_regions(std::string_view device) noexcept -> std::vector<FreeRegion> {
    const auto cmd = fmt::format(FMT_COMPILE("parted -m -s '{}' unit B print free 2>/dev/null"), device);
    return parse_free_regions(utils::exec(cmd));
}

auto create_partition_in_region(std::string_view device, std::uint64_t start_bytes, std::uint64_t end_bytes) noexcept -> Result<std::string> {
    if (end_bytes <= start_bytes) {
        return make_error(ErrorCode::InvalidArgument,
            fmt::format("free region end ({}) must be greater than start ({})", end_bytes, start_bytes));
    }

    const auto before = partition_names_under(device);

    // -a optimal lets parted snap the boundaries to alignment within the gap.
    const auto mkpart = fmt::format(
        FMT_COMPILE("parted -m -s -a optimal '{}' unit B mkpart primary {}B {}B &>>/tmp/cachyos-install.log"),
        device, start_bytes, end_bytes);
    if (!utils::exec_checked(mkpart)) {
        return make_error(ErrorCode::SubprocessFailed,
            fmt::format("failed to create partition in free region on {}", device));
    }
    const auto reprobe = fmt::format(FMT_COMPILE("partprobe '{}' &>/dev/null"), device);
    utils::exec_checked(reprobe);

    const auto after = partition_names_under(device);
    for (const auto& name : after) {
        if (!before.contains(name)) {
            return fmt::format("/dev/{}", name);
        }
    }
    return make_error(ErrorCode::NotFound,
        fmt::format("created a partition on {} but could not identify its device node", device));
}

auto gen_sfdisk_command(const std::vector<fs::Partition>& partitions, bool is_efi) noexcept -> std::string {
    // sfdisk does not create partition table without partitions by default. The lines with partitions are expected in the script by default.
    std::string sfdisk_commands{"label: gpt\n"s};
    if (!is_efi) {
        sfdisk_commands = "label: dos\n"s;
    }

    // sort by mountpoint & device
    auto partitions_sorted{partitions};
    std::ranges::sort(partitions_sorted, {}, &fs::Partition::mountpoint);
    std::ranges::sort(partitions_sorted, {}, &fs::Partition::device);

    // sort by size(empty sized parts must be at the end)
    std::ranges::sort(partitions_sorted, std::ranges::greater(), &fs::Partition::size);

    // filter duplicates
    std::vector<fs::Partition> partitions_filtered{};
    std::ranges::unique_copy(
        partitions_sorted, std::back_inserter(partitions_filtered),
        {},
        &fs::Partition::device);

    for (const auto& part : partitions_filtered) {
        const auto& fsname   = convert_fsname(part.fstype);
        const auto& fs_alias = get_part_type_alias(fsname);

        // L - alias 'linux'. Linux
        // U - alias 'uefi'. EFI System partition
        sfdisk_commands += fmt::format(FMT_COMPILE("type={}"), fs_alias);

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
            sfdisk_commands += fmt::format(FMT_COMPILE(",size={}"), part.size);
        }

        // set boot flag
        if (fsname == "vfat"sv) {
            // bootable is specified as [*|-], with as default not-bootable.
            // The value of this field is irrelevant for Linux
            // - when Linux runs it has been booted already
            // - but it might play a role for certain boot loaders and for other operating systems.
            sfdisk_commands += ",bootable"s;
        }
        sfdisk_commands += "\n"s;
    }
    return sfdisk_commands;
}

auto run_sfdisk_part(std::string_view commands, std::string_view device) noexcept -> Result<void> {
    const auto& sfdisk_cmd = fmt::format(FMT_COMPILE("echo -e '{}' | sfdisk -w always '{}' &>>/tmp/cachyos-install.log &>/dev/null"), commands, device);
    if (!utils::exec_checked(sfdisk_cmd)) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to run partitioning with sfdisk: {}", sfdisk_cmd));
    }
    return {};
}

auto erase_disk(std::string_view device) noexcept -> Result<void> {
    // 1. write zeros
    const auto& dd_cmd = fmt::format(FMT_COMPILE("dd if=/dev/zero of='{}' bs=512 count=1 2>>/tmp/cachyos-install.log &>/dev/null"), device);
    if (!utils::exec_checked(dd_cmd)) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to run dd on disk: {}", dd_cmd));
    }
    // 2. run wipefs on disk
    const auto& wipe_cmd = fmt::format(FMT_COMPILE("wipefs -af '{}' 2>>/tmp/cachyos-install.log &>/dev/null"), device);
    if (!utils::exec_checked(wipe_cmd)) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to run wipefs on disk: {}", wipe_cmd));
    }
    // 3. clear all data and destroy GPT data structures
    const auto& sgdisk_cmd = fmt::format(FMT_COMPILE("sgdisk -Zo '{}' 2>>/tmp/cachyos-install.log &>/dev/null"), device);
    if (!utils::exec_checked(sgdisk_cmd)) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to run sgdisk on disk: {}", sgdisk_cmd));
    }

    return {};
}

auto generate_default_partition_schema(std::string_view device, std::string_view boot_mountpoint, bool is_efi, std::string_view efi_partition_size) noexcept -> std::vector<fs::Partition> {
    // TODO(vnepogodin): make whole default partition scheme customizable from config/code

    // Create the ESP (size driven by the bootloader, e.g. grub 512MiB / limine 4GiB)
    // only for UEFI systems:
    fs::DefaultPartitionSchemaConfig config{
        // TODO(vnepogodin): currently doesn't matter which FS is used here for sgdisk, make customizable for future use
        .root_fs_type       = fs::FilesystemType::Btrfs,
        .efi_partition_size = std::string{efi_partition_size},
        .is_ssd             = gucc::disk::is_device_ssd(device),
        .boot_mountpoint    = std::string{boot_mountpoint},
    };
    return generate_partition_schema_from_config(device, config, is_efi);
}

auto make_clean_partschema(std::string_view device, const std::vector<fs::Partition>& partitions, bool is_efi) noexcept -> Result<void> {
    // clear disk
    if (auto res = erase_disk(device); !res) {
        return res;
    }
    // apply schema
    const auto& sfdisk_commands = gucc::disk::gen_sfdisk_command(partitions, is_efi);
    if (auto res = run_sfdisk_part(sfdisk_commands, device); !res) {
        return res;
    }
    return {};
}

auto generate_partition_schema_from_config(std::string_view device, const fs::DefaultPartitionSchemaConfig& config, bool is_efi) noexcept -> std::vector<fs::Partition> {
    // NOTE(vnepogodin): function partition schema assumes we will use whole drive, and ignores ZFS or BTRFS
    std::vector<fs::Partition> partitions{};

    // Get root mount opts from config or use defaults
    const auto& root_mount_opts = config.root_mount_opts.value_or(
        fs::get_default_mount_opts(config.root_fs_type, config.is_ssd));

    // currently partition uuid doesn't matter, it will be assigned much after during FS partitioning

    // For UEFI: create EFI partition first
    if (is_efi) {
        fs::Partition efi_partition{
            .fstype     = "vfat"s,
            .mountpoint = config.boot_mountpoint,
            .uuid_str   = {},
            .device     = insert_partition_number(device, static_cast<std::uint32_t>(partitions.size() + 1)),
            .size       = config.efi_partition_size,
            .mount_opts = fs::get_default_mount_opts(fs::FilesystemType::Vfat, config.is_ssd)};
        partitions.emplace_back(std::move(efi_partition));
    } else if (config.boot_partition_size) {
        // For BIOS with separate boot partition
        fs::Partition boot_partition{
            .fstype     = "ext4"s,
            .mountpoint = config.boot_mountpoint,
            .uuid_str   = {},
            .device     = insert_partition_number(device, static_cast<std::uint32_t>(partitions.size() + 1)),
            .size       = *config.boot_partition_size,
            .mount_opts = fs::get_default_mount_opts(fs::FilesystemType::Ext4, config.is_ssd)};
        partitions.emplace_back(std::move(boot_partition));
    }

    // Create swap partition if configured
    if (config.swap_partition_size) {
        fs::Partition swap_partition{
            .fstype     = "linuxswap"s,
            .mountpoint = ""s,
            .uuid_str   = {},
            .device     = insert_partition_number(device, static_cast<std::uint32_t>(partitions.size() + 1)),
            .size       = *config.swap_partition_size,
            .mount_opts = "defaults"s};
        partitions.emplace_back(std::move(swap_partition));
    }

    // Create root partition (uses remaining space)
    fs::Partition root_partition{
        .fstype     = std::string{fs::filesystem_type_to_string(config.root_fs_type)},
        .mountpoint = "/"s,
        .uuid_str   = {},
        .device     = insert_partition_number(device, static_cast<std::uint32_t>(partitions.size() + 1)),
        .size       = {},
        .mount_opts = root_mount_opts};
    partitions.emplace_back(std::move(root_partition));

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
    bool has_root = std::ranges::any_of(partitions, [](auto&& part) { return part.mountpoint == "/"sv; });
    if (!has_root) {
        result.is_valid = false;
        result.errors.emplace_back("No root (/) partition defined");
    }

    // Check for EFI partition on UEFI systems
    bool has_efi = std::ranges::any_of(partitions, [](auto&& part) { return part.fstype == "vfat"sv || part.fstype == "fat32"sv || part.fstype == "fat16"sv; });
    if (is_efi && !has_efi) {
        result.is_valid = false;
        result.errors.emplace_back("UEFI system requires an EFI partition (vfat/fat32)");
    }

    // Invalid partition sizes
    auto empty_size_count = std::ranges::count_if(partitions, [](auto&& part) { return part.size.empty() && !part.subvolume; });
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

    for (const auto& part : partitions) {
        // skip subvolumes
        if (part.subvolume) {
            continue;
        }
        const auto& size_str  = part.size.empty() ? "<remaining>" : part.size;
        const auto& mount_str = part.mountpoint.empty() ? "-" : part.mountpoint;
        const auto& opts_str  = part.mount_opts.empty() ? "defaults" : part.mount_opts;

        // lets truncate long options..
        const auto& display_opts = (opts_str.size() > 30) ? opts_str.substr(0, 27) + "..." : opts_str;

        preview += fmt::format(FMT_COMPILE("{:<20} {:<12} {:<12} {:<15} {}\n"),
            part.device, size_str, part.fstype, mount_str, display_opts);
    }

    // pretty-print the subvols
    bool has_subvols = false;
    for (const auto& part : partitions) {
        if (!part.subvolume) {
            continue;
        }
        if (!has_subvols) {
            preview += "\nBtrfs Subvolumes:\n";
            preview += fmt::format(FMT_COMPILE("{:<20} {:<15}\n"), "Subvolume", "Mount Point");
            preview += std::string(40, '-') + "\n";
            has_subvols = true;
        }
        preview += fmt::format(FMT_COMPILE("{:<20} {:<15}\n"),
            *part.subvolume, part.mountpoint);
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

}  // namespace gucc::disk
