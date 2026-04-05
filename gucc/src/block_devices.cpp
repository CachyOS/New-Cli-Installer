#include "gucc/block_devices.hpp"
#include "gucc/io_utils.hpp"

#include <algorithm>  // for find, find_if, equal
#include <cctype>     // for tolower
#include <ranges>     // for ranges::*
#include <set>        // for set

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

using BlockDevice = gucc::disk::BlockDevice;

/// Constructs a BlockDevice from a RapidJSON object.
auto get_blockdevice_from_json(const rapidjson::Value& doc) -> BlockDevice {
    auto device = BlockDevice{};
    if (doc.HasMember("name") && doc["name"].IsString()) {
        device.name = doc["name"].GetString();
    }
    if (doc.HasMember("type") && doc["type"].IsString()) {
        device.type = doc["type"].GetString();
    }
    if (doc.HasMember("fstype") && doc["fstype"].IsString()) {
        device.fstype = doc["fstype"].GetString();
    }
    if (doc.HasMember("uuid") && doc["uuid"].IsString()) {
        device.uuid = doc["uuid"].GetString();
    }

    if (doc.HasMember("model") && doc["model"].IsString()) {
        device.model = doc["model"].GetString();
    }
    if (doc.HasMember("mountpoints") && doc["mountpoints"].IsArray()) {
        for (const auto& mnt : doc["mountpoints"].GetArray()) {
            if (!mnt.IsString()) {
                continue;
            }
            auto mountpoint = std::string{mnt.GetString()};
            if (!mountpoint.empty()) {
                device.mountpoints.emplace_back(std::move(mountpoint));
            }
        }
    } else if (doc.HasMember("mountpoint") && doc["mountpoint"].IsString()) {
        auto mountpoint = std::string{doc["mountpoint"].GetString()};
        if (!mountpoint.empty()) {
            device.mountpoints.emplace_back(std::move(mountpoint));
        }
    }
    if (doc.HasMember("pkname") && doc["pkname"].IsString()) {
        device.pkname = doc["pkname"].GetString();
    }
    if (doc.HasMember("label") && doc["label"].IsString()) {
        device.label = doc["label"].GetString();
    }
    if (doc.HasMember("partuuid") && doc["partuuid"].IsString()) {
        device.partuuid = doc["partuuid"].GetString();
    }
    if (doc.HasMember("size") && doc["size"].IsInt64()) {
        device.size = doc["size"].GetInt64();
    }

    return device;
}

auto parse_lsblk_json(std::string_view json_output) -> std::vector<BlockDevice> {
    rapidjson::Document document;

    document.Parse(json_output.data(), json_output.size());
    if (document.HasParseError()) {
        spdlog::error("Failed to parse lsblk output: {}", rapidjson::GetParseError_En(document.GetParseError()));
        return {};
    }

    // Extract data from JSON
    std::vector<BlockDevice> block_devices{};
    if (document.HasMember("blockdevices") && document["blockdevices"].IsArray()) {
        for (const auto& device_json : document["blockdevices"].GetArray()) {
            auto block_device = get_blockdevice_from_json(device_json);
            block_devices.emplace_back(std::move(block_device));
        }
    }
    return block_devices;
}

}  // namespace

namespace gucc::disk {

auto find_device_by_name(const std::vector<BlockDevice>& devices, std::string_view device_name) -> std::optional<BlockDevice> {
    auto it = std::ranges::find(devices, device_name, &BlockDevice::name);
    if (it != std::ranges::end(devices)) {
        return *it;
    }
    return std::nullopt;
}

auto find_device_by_pkname(const std::vector<BlockDevice>& devices, std::string_view pkname) -> std::optional<BlockDevice> {
    auto it = std::ranges::find_if(devices, [pkname](const auto& dev) {
        return dev.pkname == pkname;
    });
    if (it != std::ranges::end(devices)) {
        return *it;
    }
    return std::nullopt;
}

auto find_device_by_mountpoint(const std::vector<BlockDevice>& devices, std::string_view mountpoint) -> std::optional<BlockDevice> {
    auto it = std::ranges::find_if(devices, [mountpoint](const auto& dev) {
        return std::ranges::contains(dev.mountpoints, mountpoint);
    });
    if (it != std::ranges::end(devices)) {
        return *it;
    }
    return std::nullopt;
}

auto has_type_in_ancestry(const std::vector<BlockDevice>& devices, std::string_view device_name, std::string_view type) -> bool {
    return find_ancestor_of_type(devices, device_name, type).has_value();
}

auto find_ancestor_of_type(const std::vector<BlockDevice>& devices, std::string_view device_name, std::string_view type) -> std::optional<BlockDevice> {
    std::set<std::string> visited;
    auto current_name = std::string{device_name};
    while (!visited.contains(current_name)) {
        visited.insert(current_name);

        auto it = std::ranges::find(devices, current_name, &BlockDevice::name);
        if (it == std::ranges::end(devices)) {
            break;
        }
        if (it->type == type) {
            return *it;
        }
        if (!it->pkname) {
            break;
        }
        current_name = *it->pkname;
    }
    return std::nullopt;
}

auto find_devices_by_type(const std::vector<BlockDevice>& devices, std::string_view type) -> std::vector<BlockDevice> {
    auto filtered = devices | std::ranges::views::filter([type](const auto& dev) { return dev.type == type; });
    return {filtered.begin(), filtered.end()};
}

auto find_devices_by_type_and_fstype(const std::vector<BlockDevice>& devices, std::string_view type, std::string_view fstype) -> std::vector<BlockDevice> {
    auto ci_equal = [](std::string_view a, std::string_view b) {
        return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) {
            return std::tolower(x) == std::tolower(y);
        });
    };
    auto filtered = devices | std::ranges::views::filter([&](const auto& dev) {
        return dev.type == type && ci_equal(dev.fstype, fstype);
    });
    return {filtered.begin(), filtered.end()};
}

auto list_block_devices() -> std::optional<std::vector<BlockDevice>> {
    const auto& lsblk_output = utils::exec(R"cmd(lsblk -f -o NAME,TYPE,FSTYPE,UUID,PARTUUID,PKNAME,LABEL,SIZE,MOUNTPOINTS,MODEL -b -p -a -J -Q "(type=='part') || (type=='crypt' && fstype) || (type=='lvm')")cmd");
    if (lsblk_output.empty()) {
        return std::nullopt;
    }
    return std::make_optional<std::vector<BlockDevice>>(parse_lsblk_json(lsblk_output));
}

}  // namespace gucc::disk
