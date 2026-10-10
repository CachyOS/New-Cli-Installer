#include "gucc/system_query.hpp"
#include "gucc/block_devices.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/partition.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/zfs.hpp"

#include "detail/lsblk_json.hpp"

#include <algorithm>   // for find_if, transform
#include <fstream>     // for ifstream
#include <functional>  // for function_ref
#include <ranges>      // for ranges::*

#include <rapidjson/document.h>

#include <fmt/compile.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

static constexpr auto DEV_PATH_PREFIX  = "/dev/"sv;
static constexpr auto TRAILING_NUMBERS = "0123456789"sv;
static constexpr auto LSBLK_DISKS_CMD  = "lsblk -J -b -o NAME,TYPE,SIZE,MODEL,FSTYPE,LABEL,UUID,PARTUUID,MOUNTPOINTS,PTTYPE,RM,RO,TRAN -p"sv;

namespace {

/// lsblk leaves TRAN empty for virtual disks, so fall back to the device name.
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

namespace json = gucc::detail::lsblk_json;

auto has_type(const rapidjson::Value& obj, std::string_view type) noexcept -> bool {
    return json::string(obj, "type"sv) == type;
}

auto get_partition_from_json(const rapidjson::Value& obj) -> gucc::disk::PartitionInfo {
    gucc::disk::PartitionInfo part{};
    part.device   = json::string(obj, "name"sv).value_or(""sv);
    part.fstype   = json::string(obj, "fstype"sv).value_or(""sv);
    part.label    = json::owned_string(obj, "label"sv);
    part.uuid     = json::owned_string(obj, "uuid"sv);
    part.partuuid = json::owned_string(obj, "partuuid"sv);
    part.size     = json::uint64(obj, "size"sv).value_or(0);

    part.mountpoints = json::mountpoints(obj);
    part.is_mounted  = !part.mountpoints.empty();
    part.part_number = gucc::disk::parse_partition_number(part.device);
    return part;
}

auto get_disk_from_json(const rapidjson::Value& obj, std::function_ref<bool(std::string_view)> is_ssd) -> gucc::disk::DiskInfo {
    gucc::disk::DiskInfo disk{};
    disk.device       = json::string(obj, "name"sv).value_or(""sv);
    disk.model        = json::owned_string(obj, "model"sv);
    disk.size         = json::uint64(obj, "size"sv).value_or(0);
    disk.pttype       = json::owned_string(obj, "pttype"sv);
    disk.is_removable = json::boolean(obj, "rm"sv).value_or(false);
    disk.transport    = determine_transport(disk.device, json::string(obj, "tran"sv).value_or(""sv));
    disk.is_ssd       = is_ssd(disk.device);

    for (const auto& child : json::array(obj, "children"sv)) {
        if (has_type(child, "part"sv)) {
            disk.partitions.emplace_back(get_partition_from_json(child));
        }
    }
    return disk;
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

    rapidjson::Document document;
    if (!json::parse_or_log(document, json_output)) {
        return {};
    }

    std::vector<DiskInfo> disks{};
    for (const auto& device_json : json::array(document, "blockdevices"sv)) {
        if (has_type(device_json, "disk"sv)) {
            disks.emplace_back(get_disk_from_json(device_json, is_ssd));
        }
    }
    return disks;
}

auto list_disks() noexcept -> std::optional<std::vector<DiskInfo>> {
    // Query all disk devices with their partitions
    const auto& lsblk_output = utils::exec(LSBLK_DISKS_CMD);

    if (lsblk_output.empty()) {
        spdlog::error("Failed to get lsblk output");
        return std::nullopt;
    }

    auto disks = parse_lsblk_disks_json(lsblk_output);
    if (disks.empty()) {
        spdlog::warn("No disks found from lsblk");
    }

    return std::make_optional<std::vector<DiskInfo>>(std::move(disks));
}

auto get_disk_info(std::string_view device) noexcept -> std::optional<DiskInfo> {
    const auto& lsblk_output = utils::exec(fmt::format(FMT_COMPILE("{} {}"), LSBLK_DISKS_CMD, device));

    if (lsblk_output.empty()) {
        spdlog::error("Failed to get lsblk output for device: {}", device);
        return std::nullopt;
    }

    auto disks = parse_lsblk_disks_json(lsblk_output);
    if (disks.empty()) {
        return std::nullopt;
    }

    return std::make_optional<DiskInfo>(std::move(disks.front()));
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
    constexpr std::uint64_t KiB = 1024ULL;
    constexpr std::uint64_t MiB = KiB * 1024;
    constexpr std::uint64_t GiB = MiB * 1024;
    constexpr std::uint64_t TiB = GiB * 1024;

    if (bytes >= TiB) {
        return fmt::format(FMT_COMPILE("{:.1f}TiB"), static_cast<double>(bytes) / TiB);
    } else if (bytes >= GiB) {
        return fmt::format(FMT_COMPILE("{:.1f}GiB"), static_cast<double>(bytes) / GiB);
    } else if (bytes >= MiB) {
        return fmt::format(FMT_COMPILE("{:.0f}MiB"), static_cast<double>(bytes) / MiB);
    } else if (bytes >= KiB) {
        return fmt::format(FMT_COMPILE("{:.0f}KiB"), static_cast<double>(bytes) / KiB);
    }
    return fmt::format(FMT_COMPILE("{}B"), bytes);
}

auto is_device_ssd(std::string_view device) noexcept -> bool {
    const auto base_device     = get_disk_name_from_device(device);
    const auto rotational_path = fmt::format(FMT_COMPILE("/sys/block/{}/queue/rotational"), base_device);

    // Note: can't use file_utils::read_whole_file because sysfs files report size 0
    std::ifstream file(rotational_path);
    if (!file.is_open()) {
        return base_device.starts_with("nvme"sv) || base_device.starts_with("vd"sv);
    }

    char rotational{};
    file >> rotational;
    return rotational == '0';
}

auto format_partition_table(std::string_view disk_device) noexcept -> std::string {
    auto disk_info = get_disk_info(disk_device);
    if (!disk_info) {
        return {};
    }

    auto result = fmt::format("{:<20} {:<8} {:<12} {}\n", "NAME", "TYPE", "FSTYPE", "SIZE");
    result += fmt::format("{:<20} {:<8} {:<12} {}\n", disk_info->device, "disk", "", format_size(disk_info->size));
    for (const auto& part : disk_info->partitions) {
        result += fmt::format("{:<20} {:<8} {:<12} {}\n", part.device, "part", part.fstype, format_size(part.size));
    }
    return result;
}

auto format_device_table() noexcept -> std::string {
    auto disks = list_disks();
    if (!disks) {
        return {};
    }

    auto result = fmt::format("{:<20} {:<16} {:<8} {:<12} {:<10} {}\n", "NAME", "MODEL", "TYPE", "FSTYPE", "SIZE", "MOUNTPOINTS");
    for (const auto& disk : *disks) {
        const auto& model = disk.model.value_or("");
        result += fmt::format("{:<20} {:<16} {:<8} {:<12} {:<10} {}\n", disk.device, model, "disk", "", format_size(disk.size), "");
        for (const auto& part : disk.partitions) {
            const auto& mp = part.mountpoints.empty() ? ""s : fmt::format("{}", fmt::join(part.mountpoints, ", "));
            result += fmt::format("{:<20} {:<16} {:<8} {:<12} {:<10} {}\n", part.device, "", "part", part.fstype, format_size(part.size), mp);
        }
    }
    return result;
}

auto list_mountable_partitions(const std::vector<std::string>& include_types) noexcept -> std::vector<std::string> {
    std::vector<std::string> result{};

    const auto& devices = list_block_devices();
    if (devices) {
        const auto append_of_type = [&](std::string_view type) {
            if (std::ranges::find(include_types, type) == std::ranges::end(include_types)) {
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
    constexpr auto to_entry = [](std::string_view line) -> ZvolEntry {
        const auto tab_pos = line.find('\t');
        return ZvolEntry{
            .name    = line.substr(0, tab_pos),
            .volsize = tab_pos == std::string_view::npos ? std::string_view{} : line.substr(tab_pos + 1),
        };
    };
    return utils::make_split_view(output)
        | std::ranges::views::transform(to_entry)
        | std::ranges::to<std::vector<ZvolEntry>>();
}

}  // namespace gucc::disk::detail
