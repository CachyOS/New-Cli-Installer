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
        sfdisk_commands = "label: msdos\n"s;
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
        sfdisk_commands += fmt::format(FMT_COMPILE(",type={}"), fs_alias);

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
    const auto& sfdisk_cmd = fmt::format(FMT_COMPILE("echo -e '{}' | sfdisk '{}' 2>>/tmp/cachyos-install.log &>/dev/null"), commands, device);
    if (!utils::exec_checked(sfdisk_cmd)) {
        spdlog::error("Failed to run partitioning with sfdisk: {}", sfdisk_cmd);
        return false;
    }
    return true;
}

}  // namespace gucc::disk
