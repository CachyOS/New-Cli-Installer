#include "gucc/system_query.hpp"
#include "gucc/block_devices.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/memory.hpp"
#include "gucc/partition.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/zfs.hpp"

#include <algorithm>   // for find_if, transform
#include <functional>  // for function_ref
#include <ranges>      // for ranges::*

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

static constexpr auto DEV_PATH_PREFIX  = "/dev/"sv;
static constexpr auto TRAILING_NUMBERS = "0123456789"sv;

namespace {

// lsblk leaves TRAN empty for virtual disks, so fall back to the device name.
auto determine_transport(std::string_view device, std::string_view tran) noexcept -> gucc::disk::DiskTransport {
    using gucc::disk::DiskTransport;

    if (const auto transport = gucc::disk::string_to_disk_transport(tran); transport != DiskTransport::Unknown) {
        return transport;
    }
    if (device.contains("nvme"sv)) {
        return DiskTransport::Nvme;
    } else if (device.contains("vd"sv)) {
        return DiskTransport::Virtio;
    }

    return DiskTransport::Unknown;
}

using gucc::disk::BlockDevice;

auto to_partition_info(const BlockDevice& dev) -> gucc::disk::PartitionInfo {
    return gucc::disk::PartitionInfo{
        .device      = dev.name,
        .fstype      = dev.fstype,
        .label       = dev.label,
        .uuid        = dev.uuid.empty() ? std::nullopt : std::optional{dev.uuid},
        .partuuid    = dev.partuuid,
        .size        = dev.size.value_or(0),
        .mountpoints = dev.mountpoints,
        .is_mounted  = !dev.mountpoints.empty(),
        .part_number = gucc::disk::parse_partition_number(dev.name),
    };
}

auto to_disk_info(const BlockDevice& disk, const std::vector<BlockDevice>& devices, std::function_ref<bool(std::string_view)> is_ssd) -> gucc::disk::DiskInfo {
    return gucc::disk::DiskInfo{
        .device       = disk.name,
        .model        = disk.model,
        .size         = disk.size.value_or(0),
        .transport    = determine_transport(disk.name, disk.transport),
        .is_ssd       = is_ssd(disk.name),
        .is_removable = disk.removable,
        .pttype       = disk.pttype,
        .partitions   = devices
            | std::ranges::views::filter([&disk](const BlockDevice& dev) { return dev.type == "part"sv && dev.pkname == disk.name; })
            | std::ranges::views::transform(to_partition_info)
            | std::ranges::to<std::vector<gucc::disk::PartitionInfo>>(),
    };
}

auto query_disks(std::string_view device) noexcept -> std::optional<std::vector<gucc::disk::DiskInfo>> {
    auto output = gucc::utils::query_output(fmt::format(FMT_COMPILE("lsblk -J -b -p -o {} {}"), gucc::disk::detail::kLsblkColumns, device));
    if (!output) {
        spdlog::error("lsblk failed for '{}'", device);
        return std::nullopt;
    }
    return gucc::disk::parse_lsblk_disks_json(*output);
}

}  // namespace

namespace gucc::disk {

auto parse_partition_number(std::string_view device) noexcept -> std::uint32_t {
    if (device.starts_with(DEV_PATH_PREFIX)) {
        device.remove_prefix(DEV_PATH_PREFIX.size());
    }

    // nvme case with partition number always after 'p'
    if (device.starts_with("nvme"sv)) {
        if (auto pos = device.find_first_of('p'); pos != std::string_view::npos) {
            return utils::parse_uint<std::uint32_t>(device.substr(pos + 1)).value_or(0);
        }
        return 0;
    }

    // regular case
    auto pos = device.find_last_not_of(TRAILING_NUMBERS);
    if (pos == std::string_view::npos || pos == device.size() - 1) {
        return 0;
    }
    return utils::parse_uint<std::uint32_t>(device.substr(pos + 1)).value_or(0);
}

auto insert_partition_number(std::string_view device, std::uint32_t part_number) noexcept -> std::string {
    // preserve device path
    const auto& device_path = [device]() {
        bool use_full_path = device.starts_with(DEV_PATH_PREFIX);
        auto disk_name     = get_disk_name_from_device(device);
        return use_full_path ? fmt::format(FMT_COMPILE("/dev/{}"), disk_name) : std::string{disk_name};
    }();
    // nvme case with partition number always after 'p'
    if (device.contains("nvme"sv)) {
        return fmt::format(FMT_COMPILE("{}p{}"), device_path, part_number);
    }

    // regular case
    return fmt::format(FMT_COMPILE("{}{}"), device_path, part_number);
}

auto get_disk_name_from_device(std::string_view device) noexcept -> std::string_view {
    if (device.starts_with(DEV_PATH_PREFIX)) {
        device.remove_prefix(DEV_PATH_PREFIX.size());
    }

    if (device.starts_with("nvme"sv)) {
        if (auto pos = device.find_first_of('p'); pos != std::string_view::npos) {
            return device.substr(0, pos);
        }
        return device;
    }

    auto pos = device.find_last_not_of(TRAILING_NUMBERS);
    return (pos != std::string_view::npos) ? device.substr(0, pos + 1) : device;
}

auto disk_transport_to_string(DiskTransport transport) noexcept -> std::string_view {
    switch (transport) {
    case DiskTransport::Sata:
        return "sata"sv;
    case DiskTransport::Nvme:
        return "nvme"sv;
    case DiskTransport::Usb:
        return "usb"sv;
    case DiskTransport::Scsi:
        return "scsi"sv;
    case DiskTransport::Virtio:
        return "virtio"sv;
    case DiskTransport::Unknown:
    default:
        return "unknown"sv;
    }
}

auto string_to_disk_transport(std::string_view transport_str) noexcept -> DiskTransport {
    if (transport_str == "sata"sv || transport_str == "ata"sv) {
        return DiskTransport::Sata;
    } else if (transport_str == "nvme"sv) {
        return DiskTransport::Nvme;
    } else if (transport_str == "usb"sv) {
        return DiskTransport::Usb;
    } else if (transport_str == "scsi"sv || transport_str == "sas"sv) {
        return DiskTransport::Scsi;
    } else if (transport_str == "virtio"sv) {
        return DiskTransport::Virtio;
    }
    return DiskTransport::Unknown;
}

auto parse_lsblk_disks_json(std::string_view json_output, std::function_ref<bool(std::string_view)> is_ssd) noexcept -> std::vector<DiskInfo> {
    if (json_output.empty()) {
        return {};
    }
    const auto devices = detail::parse_lsblk_json(json_output);
    if (!devices) {
        return {};
    }
    return *devices
        | std::ranges::views::filter([](const BlockDevice& dev) { return dev.type == "disk"sv; })
        | std::ranges::views::transform([&](const BlockDevice& disk) { return to_disk_info(disk, *devices, is_ssd); })
        | std::ranges::to<std::vector<DiskInfo>>();
}

auto list_disks() noexcept -> std::optional<std::vector<DiskInfo>> {
    return query_disks({});
}

auto get_disk_info(std::string_view device) noexcept -> std::optional<DiskInfo> {
    return query_disks(device).and_then([](std::vector<DiskInfo> disks) {
        return disks.empty() ? std::nullopt : std::optional{std::move(disks.front())};
    });
}

auto list_partitions(std::string_view disk_device) noexcept -> std::vector<PartitionInfo> {
    auto disk_info = get_disk_info(disk_device);
    if (!disk_info) {
        return {};
    }
    return std::move(disk_info->partitions);
}

auto get_partition_schema(std::string_view disk_device) noexcept -> std::vector<fs::Partition> {
    auto partitions = list_partitions(disk_device);
    if (partitions.empty()) {
        return {};
    }

    std::vector<fs::Partition> schema{};
    schema.reserve(partitions.size());

    for (const auto& part : partitions) {
        fs::Partition p{};
        p.device     = part.device;
        p.fstype     = part.fstype;
        p.mountpoint = part.mountpoints.empty() ? ""s : part.mountpoints.front();
        p.uuid_str   = part.uuid.value_or("");
        // size needs to be converted to human readable format for sfdisk
        if (part.size > 0) {
            p.size = format_size(part.size);
        }
        // needs to be queried later on
        p.mount_opts = "defaults"s;

        schema.emplace_back(std::move(p));
    }

    return schema;
}

auto format_size(std::uint64_t bytes) noexcept -> std::string {
    using memory::kGiB, memory::kKiB, memory::kMiB, memory::kTiB;

    if (bytes >= kTiB) {
        return fmt::format(FMT_COMPILE("{:.1f}TiB"), static_cast<double>(bytes) / kTiB);
    } else if (bytes >= kGiB) {
        return fmt::format(FMT_COMPILE("{:.1f}GiB"), static_cast<double>(bytes) / kGiB);
    } else if (bytes >= kMiB) {
        return fmt::format(FMT_COMPILE("{:.0f}MiB"), static_cast<double>(bytes) / kMiB);
    } else if (bytes >= kKiB) {
        return fmt::format(FMT_COMPILE("{:.0f}KiB"), static_cast<double>(bytes) / kKiB);
    }
    return fmt::format(FMT_COMPILE("{}B"), bytes);
}

auto is_device_ssd(std::string_view device) noexcept -> bool {
    const auto base_device     = get_disk_name_from_device(device);
    const auto rotational_path = fmt::format(FMT_COMPILE("/sys/block/{}/queue/rotational"), base_device);

    const auto rotational = file_utils::read_first_line(rotational_path);
    if (rotational.empty()) {
        return base_device.starts_with("nvme"sv) || base_device.starts_with("vd"sv);
    }
    return rotational.starts_with('0');
}

auto format_partition_table(std::string_view disk_device) noexcept -> std::string {
    auto disk_info = get_disk_info(disk_device);
    if (!disk_info) {
        return {};
    }

    auto result = fmt::format(FMT_COMPILE("{:<20} {:<8} {:<12} {}\n"), "NAME", "TYPE", "FSTYPE", "SIZE");
    result += fmt::format(FMT_COMPILE("{:<20} {:<8} {:<12} {}\n"), disk_info->device, "disk", "", format_size(disk_info->size));
    for (const auto& part : disk_info->partitions) {
        result += fmt::format(FMT_COMPILE("{:<20} {:<8} {:<12} {}\n"), part.device, "part", part.fstype, format_size(part.size));
    }
    return result;
}

auto format_device_table() noexcept -> std::string {
    auto disks = list_disks();
    if (!disks) {
        return {};
    }

    auto result = fmt::format(FMT_COMPILE("{:<20} {:<16} {:<8} {:<12} {:<10} {}\n"), "NAME", "MODEL", "TYPE", "FSTYPE", "SIZE", "MOUNTPOINTS");
    for (const auto& disk : *disks) {
        const auto& model = disk.model.value_or("");
        result += fmt::format(FMT_COMPILE("{:<20} {:<16} {:<8} {:<12} {:<10} {}\n"), disk.device, model, "disk", "", format_size(disk.size), "");
        for (const auto& part : disk.partitions) {
            result += fmt::format(FMT_COMPILE("{:<20} {:<16} {:<8} {:<12} {:<10} {}\n"), part.device, "", "part", part.fstype, format_size(part.size), utils::join(part.mountpoints, ", "sv));
        }
    }
    return result;
}

auto list_mountable_partitions(std::span<const std::string_view> include_types) noexcept -> std::vector<std::string> {
    std::vector<std::string> result{};

    const auto& devices = list_block_devices();
    if (devices) {
        const auto append_of_type = [&](std::string_view type) {
            if (!std::ranges::contains(include_types, type)) {
                return;
            }
            for (const auto& dev : *devices) {
                if (dev.type != type) {
                    continue;
                }
                const auto& size_str = dev.size.has_value() ? format_size(*dev.size) : std::string{};
                result.emplace_back(fmt::format(FMT_COMPILE("{} {}"), dev.name, size_str));
            }
        };
        append_of_type("crypt"sv);
        append_of_type("lvm"sv);
        append_of_type("part"sv);
    }

    // ZFS zvols are always listed
    const auto zvol_output = fs::zfs_list_datasets("zvol"sv);
    for (const auto& [name, volsize] : detail::parse_zvol_list(zvol_output)) {
        result.emplace_back(fmt::format(FMT_COMPILE("/dev/zvol/{} {}"), name, volsize));
    }

    return result;
}

}  // namespace gucc::disk

namespace gucc::disk::detail {

auto parse_zvol_list(std::string_view output) noexcept -> std::vector<ZvolEntry> {
    // NAME<TAB>VOLSIZE
    constexpr auto to_entry = [](std::string_view line) -> ZvolEntry {
        const auto [name, volsize] = utils::split_once(line, '\t').value_or(std::pair{line, std::string_view{}});
        return ZvolEntry{.name = name, .volsize = volsize};
    };
    return utils::make_split_view(output)
        | std::ranges::views::transform(to_entry)
        | std::ranges::to<std::vector<ZvolEntry>>();
}

}  // namespace gucc::disk::detail
