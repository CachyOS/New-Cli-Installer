#include "gucc/fs_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <optional>  // for optional
#include <utility>   // for move

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_view_literals;

namespace {

struct LsblkPair {
    std::string_view key;
    std::string_view value;
    std::string_view rest;
};


auto unescape_lsblk_value(std::string_view value) noexcept -> std::string {
    return gucc::utils::decode_escapes(value, "\\x"sv, 16, 2);
}

constexpr auto next_pair(std::string_view line) noexcept -> std::optional<LsblkPair> {
    line          = gucc::utils::ltrim(line);
    const auto eq = line.find("=\""sv);
    if (eq == std::string_view::npos) {
        return std::nullopt;
    }
    const auto key = line.substr(0, eq);
    line.remove_prefix(eq + 2);
    // lsblk escapes quotes inside values
    const auto close = line.find('"');
    if (close == std::string_view::npos || key.empty() || key.contains(' ')) {
        return std::nullopt;
    }
    return LsblkPair{.key = key, .value = line.substr(0, close), .rest = line.substr(close + 1)};
}

auto parse_pairs_line(std::string_view line) noexcept -> gucc::fs::utils::detail::LsblkPairs {
    gucc::fs::utils::detail::LsblkPairs pairs{};
    for (auto pair = next_pair(line); pair; pair = next_pair(pair->rest)) {
        pairs.try_emplace(std::string{pair->key}, unescape_lsblk_value(pair->value));
    }
    return pairs;
}

}  // namespace

namespace gucc::fs::utils::detail {

auto parse_lsblk_pairs(std::string_view output) noexcept -> LsblkPairs {
    for (const auto line : gucc::utils::make_split_view(output)) {
        if (auto pairs = parse_pairs_line(line); !pairs.empty()) {
            return pairs;
        }
    }
    return {};
}

}  // namespace gucc::fs::utils::detail

namespace gucc::fs::utils {

auto get_mountpoint_fs(std::string_view mountpoint) noexcept -> std::string {
    return gucc::utils::exec(fmt::format(FMT_COMPILE("findmnt -ln -o FSTYPE \"{}\""), mountpoint));
}

auto get_mountpoint_source(std::string_view mountpoint) noexcept -> std::string {
    return gucc::utils::exec(fmt::format(FMT_COMPILE("findmnt -ln -o SOURCE \"{}\""), mountpoint));
}

auto get_device_info(std::string_view device) noexcept -> DeviceInfo {
    // e.g. FSTYPE="vfat" UUID="FB54-6E42"
    // xx
    // TODO(vnepogodin): should also use json
    auto pairs       = detail::parse_lsblk_pairs(gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -dnPo FSTYPE,UUID '{}'"), device)));
    const auto field = [&pairs](std::string_view key) {
        auto node = pairs.extract(key);
        return node.empty() ? std::string{} : std::move(node.mapped());
    };
    return DeviceInfo{.fstype = field("FSTYPE"sv), .uuid = field("UUID"sv)};
}

auto get_device_uuid(std::string_view device) noexcept -> std::string {
    return get_device_info(device).uuid;
}

auto get_device_fstype(std::string_view device) noexcept -> std::string {
    return get_device_info(device).fstype;
}

}  // namespace gucc::fs::utils
