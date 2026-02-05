#include "gucc/system_query.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/partition.hpp"

#include <algorithm>  // for find_if, transform
#include <charconv>   // for from_chars
#include <fstream>    // for ifstream
#include <ranges>     // for ranges::*

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

static constexpr auto DEV_PATH_PREFIX  = "/dev/"sv;
static constexpr auto TRAILING_NUMBERS = "0123456789"sv;

namespace {

/// Determines transport type from device path and tran field
auto determine_transport(std::string_view device, std::string_view tran) noexcept -> gucc::disk::DiskTransport {
    using gucc::disk::DiskTransport;

    if (!tran.empty()) {
        if (tran == "sata"sv || tran == "ata"sv) {
            return DiskTransport::Sata;
        } else if (tran == "nvme"sv) {
            return DiskTransport::Nvme;
        } else if (tran == "usb"sv) {
            return DiskTransport::Usb;
        } else if (tran == "scsi"sv || tran == "sas"sv) {
            return DiskTransport::Scsi;
        } else if (tran == "virtio"sv) {
            return DiskTransport::Virtio;
        }
    }

    if (device.contains("nvme"sv)) {
        return DiskTransport::Nvme;
    } else if (device.contains("vd"sv)) {
        return DiskTransport::Virtio;
    }

    return DiskTransport::Unknown;
}

auto get_partition_from_json(const rapidjson::Value& doc) -> gucc::disk::PartitionInfo {
    gucc::disk::PartitionInfo part{};

    if (doc.HasMember("name") && doc["name"].IsString()) {
        part.device = doc["name"].GetString();
    }
    if (doc.HasMember("fstype") && doc["fstype"].IsString()) {
        part.fstype = doc["fstype"].GetString();
    }
    if (doc.HasMember("label") && doc["label"].IsString()) {
        part.label = doc["label"].GetString();
    }
    if (doc.HasMember("uuid") && doc["uuid"].IsString()) {
        part.uuid = doc["uuid"].GetString();
    }
    if (doc.HasMember("partuuid") && doc["partuuid"].IsString()) {
        part.partuuid = doc["partuuid"].GetString();
    }
    if (doc.HasMember("size") && doc["size"].IsInt64()) {
        part.size = static_cast<std::uint64_t>(doc["size"].GetInt64());
    }
    if (doc.HasMember("mountpoint") && doc["mountpoint"].IsString()) {
        part.mountpoint = doc["mountpoint"].GetString();
        part.is_mounted = true;
    }

    part.part_number = gucc::disk::parse_partition_number(part.device);

    return part;
}

auto get_disk_from_json(const rapidjson::Value& doc) -> gucc::disk::DiskInfo {
    gucc::disk::DiskInfo disk{};

    if (doc.HasMember("name") && doc["name"].IsString()) {
        disk.device = doc["name"].GetString();
    }
    if (doc.HasMember("model") && doc["model"].IsString()) {
        disk.model = doc["model"].GetString();
    }
    if (doc.HasMember("size") && doc["size"].IsInt64()) {
        disk.size = static_cast<std::uint64_t>(doc["size"].GetInt64());
    }
    if (doc.HasMember("pttype") && doc["pttype"].IsString()) {
        disk.pttype = doc["pttype"].GetString();
    }
    if (doc.HasMember("rm") && doc["rm"].IsBool()) {
        disk.is_removable = doc["rm"].GetBool();
    }

    std::string_view tran{};
    if (doc.HasMember("tran") && doc["tran"].IsString()) {
        tran = doc["tran"].GetString();
    }

    disk.transport = determine_transport(disk.device, tran);
    disk.is_ssd    = gucc::disk::is_device_ssd(disk.device);

    // Parse children (partitions)
    if (doc.HasMember("children") && doc["children"].IsArray()) {
        for (const auto& child : doc["children"].GetArray()) {
            if (child.HasMember("type") && child["type"].IsString()) {
                std::string_view child_type = child["type"].GetString();
                if (child_type == "part"sv) {
                    disk.partitions.emplace_back(get_partition_from_json(child));
                }
            }
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
            auto num_str = device.substr(pos + 1);
            std::uint32_t result{0};
            std::from_chars(num_str.data(), num_str.data() + num_str.size(), result);
            return result;
        }
        return 0;
    }

    // regular case
    auto pos = device.find_last_not_of(TRAILING_NUMBERS);
    if (pos == std::string_view::npos || pos == device.size() - 1) {
        return 0;
    }
    auto num_str = device.substr(pos + 1);
    std::uint32_t result{0};
    std::from_chars(num_str.data(), num_str.data() + num_str.size(), result);
    return result;
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

auto parse_lsblk_disks_json(std::string_view json_output) noexcept -> std::vector<DiskInfo> {
    if (json_output.empty()) {
        return {};
    }

    rapidjson::Document document;
    document.Parse(json_output.data(), json_output.size());

    // Check for parse errors and ensure document is a valid object
    if (document.HasParseError()) {
        spdlog::error("Failed to parse lsblk output: {}", rapidjson::GetParseError_En(document.GetParseError()));
        return {};
    }
    if (document.IsNull() || !document.IsObject()) {
        spdlog::error("lsblk output is not a valid JSON object");
        return {};
    }

    std::vector<DiskInfo> disks{};
    if (document.HasMember("blockdevices") && document["blockdevices"].IsArray()) {
        for (const auto& device_json : document["blockdevices"].GetArray()) {
            if (device_json.HasMember("type") && device_json["type"].IsString()) {
                std::string_view dev_type = device_json["type"].GetString();
                if (dev_type == "disk"sv) {
                    auto disk = get_disk_from_json(device_json);
                    disks.emplace_back(std::move(disk));
                }
            }
        }
    }
    return disks;
}

auto list_disks() noexcept -> std::optional<std::vector<DiskInfo>> {
    // Query all disk devices with their partitions
    const auto& lsblk_output = utils::exec(
        R"(lsblk -J -b -o NAME,TYPE,SIZE,MODEL,FSTYPE,LABEL,UUID,PARTUUID,MOUNTPOINT,PTTYPE,RM,RO,TRAN -p)"sv);

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
    const auto& lsblk_output = utils::exec(
        fmt::format(FMT_COMPILE(R"(lsblk -J -b -o NAME,TYPE,SIZE,MODEL,FSTYPE,LABEL,UUID,PARTUUID,MOUNTPOINT,PTTYPE,RM,RO,TRAN -p {})"), device));

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
        p.mountpoint = part.mountpoint.value_or("");
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

}  // namespace gucc::disk
