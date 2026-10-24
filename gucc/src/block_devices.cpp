#include "gucc/block_devices.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/ranges_utils.hpp"
#include "gucc/string_utils.hpp"

#include "detail/lsblk_json.hpp"

#include <algorithm>  // for contains
#include <ranges>     // for ranges::*
#include <set>        // for set
#include <utility>    // for move

#include <rapidjson/document.h>

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

namespace json = gucc::detail::lsblk_json;

using BlockDevice = gucc::disk::BlockDevice;

auto to_block_device(const rapidjson::Value& obj) -> BlockDevice {
    auto device      = BlockDevice{};
    device.name      = json::string(obj, "name"sv).value_or(""sv);
    device.type      = json::string(obj, "type"sv).value_or(""sv);
    device.fstype    = json::string(obj, "fstype"sv).value_or(""sv);
    device.uuid      = json::string(obj, "uuid"sv).value_or(""sv);
    device.model     = json::owned_string(obj, "model"sv);
    device.pkname    = json::owned_string(obj, "pkname"sv);
    device.label     = json::owned_string(obj, "label"sv);
    device.partuuid  = json::owned_string(obj, "partuuid"sv);
    device.size      = json::uint64(obj, "size"sv);
    device.pttype    = json::owned_string(obj, "pttype"sv);
    device.transport = json::string(obj, "tran"sv).value_or(""sv);
    device.removable = json::boolean(obj, "rm"sv).value_or(false);

    // previous util-linux provided only scalar MOUNTPOINT
    if (json::member(obj, "mountpoints"sv) != nullptr) {
        device.mountpoints = json::mountpoints(obj);
    } else if (auto mountpoint = json::string(obj, "mountpoint"sv); mountpoint && !mountpoint->empty()) {
        device.mountpoints.emplace_back(*mountpoint);
    }
    return device;
}

void flatten_into(std::vector<BlockDevice>& out, const rapidjson::Value& obj, const std::optional<std::string>& parent) {
    if (!obj.IsObject()) {
        return;
    }
    auto device = to_block_device(obj);
    if (!device.pkname) {
        device.pkname = parent;
    }
    // children may hold copy..
    const auto name = std::optional{device.name};
    out.emplace_back(std::move(device));
    for (const auto& child : json::array(obj, "children"sv)) {
        flatten_into(out, child, name);
    }
}

}  // namespace

namespace gucc::disk::detail {

auto parse_lsblk_json(std::string_view json_output) noexcept -> std::optional<std::vector<BlockDevice>> {
    rapidjson::Document document;
    if (!json::parse_or_log(document, json_output)) {
        return std::nullopt;
    }
    const auto* devices_json = json::member(document, "blockdevices"sv);
    if (devices_json == nullptr || !devices_json->IsArray()) {
        spdlog::error("lsblk output has no blockdevices array");
        return std::nullopt;
    }

    std::vector<BlockDevice> block_devices{};
    for (const auto& device_json : devices_json->GetArray()) {
        flatten_into(block_devices, device_json, std::nullopt);
    }
    return block_devices;
}

}  // namespace gucc::disk::detail

namespace gucc::disk {

auto find_device_by_name(const std::vector<BlockDevice>& devices, std::string_view device_name) -> std::optional<BlockDevice> {
    return utils::find_optional(devices, device_name, &BlockDevice::name);
}

auto find_device_by_pkname(const std::vector<BlockDevice>& devices, std::string_view pkname) -> std::optional<BlockDevice> {
    return utils::find_if_optional(devices, [pkname](const BlockDevice& dev) { return dev.pkname == pkname; });
}

auto find_device_by_mountpoint(const std::vector<BlockDevice>& devices, std::string_view mountpoint) -> std::optional<BlockDevice> {
    return utils::find_if_optional(devices, [mountpoint](const BlockDevice& dev) { return std::ranges::contains(dev.mountpoints, mountpoint); });
}

auto has_type_in_ancestry(const std::vector<BlockDevice>& devices, std::string_view device_name, std::string_view type) -> bool {
    return find_ancestor_of_type(devices, device_name, type).has_value();
}

auto find_ancestor_of_type(const std::vector<BlockDevice>& devices, std::string_view device_name, std::string_view type) -> std::optional<BlockDevice> {
    std::set<std::string> visited;
    auto current_name = std::string{device_name};
    while (!visited.contains(current_name)) {
        visited.insert(current_name);

        const auto* device = utils::find_ptr(devices, current_name, &BlockDevice::name);
        if (device == nullptr) {
            break;
        }
        if (device->type == type) {
            return *device;
        }
        if (!device->pkname) {
            break;
        }
        current_name = *device->pkname;
    }
    return std::nullopt;
}

auto find_devices_by_type(const std::vector<BlockDevice>& devices, std::string_view type) -> std::vector<BlockDevice> {
    auto filtered = devices | std::ranges::views::filter([type](const auto& dev) { return dev.type == type; });
    return {filtered.begin(), filtered.end()};
}

auto find_devices_by_type_and_fstype(const std::vector<BlockDevice>& devices, std::string_view type, std::string_view fstype) -> std::vector<BlockDevice> {
    auto filtered = devices | std::ranges::views::filter([&](const auto& dev) {
        return dev.type == type && utils::iequals(dev.fstype, fstype);
    });
    return {filtered.begin(), filtered.end()};
}

auto list_block_devices() -> std::optional<std::vector<BlockDevice>> {
    return utils::query_output(fmt::format(FMT_COMPILE(R"cmd(lsblk -f -o {} -b -p -a -J -Q "(type=='part') || (type=='crypt' && fstype) || (type=='lvm')")cmd"), detail::kLsblkColumns))
        .and_then(detail::parse_lsblk_json);
}

auto query_block_device(std::string_view device) -> std::optional<BlockDevice> {
    return utils::query_output(fmt::format(FMT_COMPILE("lsblk -d -o {} -b -p -J '{}'"), detail::kLsblkColumns, device))
        .and_then(detail::parse_lsblk_json)
        .and_then([](std::vector<BlockDevice> devices) { return devices.empty() ? std::nullopt : std::optional{std::move(devices.front())}; });
}

}  // namespace gucc::disk
