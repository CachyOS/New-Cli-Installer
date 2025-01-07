#include "gucc/block_devices.hpp"
#include "gucc/io_utils.hpp"

#include <algorithm>  // for find_if
#include <ranges>     // for ranges::*
#include <utility>    // for make_optional, move

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
    if (doc.HasMember("mountpoint") && doc["mountpoint"].IsString()) {
        device.mountpoint = doc["mountpoint"].GetString();
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
    auto it = std::ranges::find_if(devices, [device_name](auto&& dev) {
        return dev.name == device_name;
    });
    if (it != std::ranges::end(devices)) {
        return std::make_optional<BlockDevice>(std::move(*it));
    }
    return std::nullopt;
}

auto list_block_devices() -> std::optional<std::vector<BlockDevice>> {
    const auto& lsblk_output = utils::exec(R"(lsblk -f -o NAME,TYPE,FSTYPE,UUID,PARTUUID,PKNAME,LABEL,SIZE,MOUNTPOINT,MODEL -b -p -a -J -Q "type=='part' || type=='crypt' && fstype")");
    if (lsblk_output.empty()) {
        return std::nullopt;
    }
    return std::make_optional<std::vector<BlockDevice>>(parse_lsblk_json(lsblk_output));
}

}  // namespace gucc::disk
