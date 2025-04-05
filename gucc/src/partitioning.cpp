#include "gucc/partitioning.hpp"
#include "gucc/io_utils.hpp"

#include <algorithm>    // for sort, unique_copy
#include <ranges>       // for ranges::*
#include <string_view>  // for string_view

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

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

}  // namespace

namespace gucc::disk {

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

auto run_sfdisk_part(std::string_view commands, std::string_view device) noexcept -> bool {
    const auto& sfdisk_cmd = fmt::format(FMT_COMPILE("echo -e '{}' | sfdisk -w always '{}' &>>/tmp/cachyos-install.log &>/dev/null"), commands, device);
    if (!utils::exec_checked(sfdisk_cmd)) {
        spdlog::error("Failed to run partitioning with sfdisk: {}", sfdisk_cmd);
        return false;
    }
    return true;
}

auto erase_disk(std::string_view device) noexcept -> bool {
    // 1. write zeros
    const auto& dd_cmd = fmt::format(FMT_COMPILE("dd if=/dev/zero of='{}' bs=512 count=1 2>>/tmp/cachyos-install.log &>/dev/null"), device);
    if (!utils::exec_checked(dd_cmd)) {
        spdlog::error("Failed to run dd on disk: {}", dd_cmd);
        return false;
    }
    // 2. run wipefs on disk
    const auto& wipe_cmd = fmt::format(FMT_COMPILE("wipefs -af '{}' 2>>/tmp/cachyos-install.log &>/dev/null"), device);
    if (!utils::exec_checked(wipe_cmd)) {
        spdlog::error("Failed to run wipefs on disk: {}", wipe_cmd);
        return false;
    }
    // 3. clear all data and destroy GPT data structures
    const auto& sgdisk_cmd = fmt::format(FMT_COMPILE("sgdisk -Zo '{}' 2>>/tmp/cachyos-install.log &>/dev/null"), device);
    if (!utils::exec_checked(sgdisk_cmd)) {
        spdlog::error("Failed to run sgdisk on disk: {}", sgdisk_cmd);
        return false;
    }

    return true;
}

auto generate_default_partition_schema(std::string_view device, std::string_view boot_mountpoint, bool is_efi) noexcept -> std::vector<fs::Partition> {
    // TODO(vnepogodin): make whole default partition scheme customizable from config/code
    fs::Partition root_partition{
        // TODO(vnepogodin): currently doesn't matter which FS is used here for sgdisk, make customizable for future use
        .fstype = "btrfs"s,
        .mountpoint = "/"s,
        // currently doesn't matter, will be assigned much after during FS partitioning
        .uuid_str = {},
        // at this moment the device is only used for sorting for sfdisk command gen
        .device = fmt::format(FMT_COMPILE("{}1"), device),
        // use remaining space
        .size = {},
        .mount_opts = {}
    };
    if (is_efi) {
        // For UEFI systems create ESP:
        //  - FAT32, 2GB
        fs::Partition efi_partition{
            .fstype = "vfat"s,
            .mountpoint = std::string{boot_mountpoint},
            // currently doesn't matter, will be assigned much after during FS partitioning
            .uuid_str = {},
            .device = root_partition.device,
            .size = "2GiB"s,
            .mount_opts = {}
        };
        // root part is the last to use all left space after boot partition
        root_partition.device = fmt::format(FMT_COMPILE("{}2"), device);
        return {std::move(efi_partition), std::move(root_partition)};
    }
    return {std::move(root_partition)};
}

auto make_clean_partschema(std::string_view device, const std::vector<fs::Partition>& partitions, bool is_efi) noexcept -> bool {
    // clear disk
    if (!erase_disk(device)) {
        spdlog::error("Failed to erase disk: {}", device);
        return false;
    }
    // apply schema
    const auto& sfdisk_commands = gucc::disk::gen_sfdisk_command(partitions, is_efi);
    if (!run_sfdisk_part(sfdisk_commands, device)) {
        spdlog::error("Failed to apply partition schema with sfdisk");
        return false;
    }
    return true;
}

}  // namespace gucc::disk
