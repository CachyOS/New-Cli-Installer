#include "gucc/mtab.hpp"
#include "gucc/conf.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/ranges_utils.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>  // for copy
#include <array>      // for array
#include <optional>
#include <ranges>  // for ranges::*
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::string_view_literals;

namespace {

auto unescape_field(std::string_view field) noexcept -> std::string {
    return gucc::utils::decode_escapes(field, "\\"sv, 8, 3);
}

}  // namespace

namespace gucc::mtab {

auto parse_mtab_content(std::string_view mtab_content, std::string_view root_mountpoint) noexcept -> std::vector<MTabEntry> {
    std::vector<MTabEntry> entries{};

    auto&& file_content_lines = utils::make_split_view(mtab_content);
    for (auto&& raw_line : file_content_lines) {
        const auto line = conf::active(raw_line);
        if (!line) {
            continue;
        }

        // e.g format: <device> <mountpoint> <fstype> <options>
        std::array<std::string_view, 4> fields{};
        const auto filled = std::ranges::copy(utils::words(*line, " "sv) | std::ranges::views::take(fields.size()), fields.begin()).out;
        if (std::ranges::distance(fields.begin(), filled) < 3) {
            continue;
        }
        const auto& [device, raw_mountpoint, fstype, options] = fields;
        if (!raw_mountpoint.contains('\\') && !raw_mountpoint.starts_with(root_mountpoint)) {
            continue;
        }
        auto mountpoint = unescape_field(raw_mountpoint);
        if (mountpoint.starts_with(root_mountpoint)) {
            entries.emplace_back(MTabEntry{
                .device     = unescape_field(device),
                .mountpoint = std::move(mountpoint),
                .fstype     = std::string{fstype},
                .options    = std::string{options},
            });
        }
    }
    return entries;
}

auto find_mount(const std::vector<MTabEntry>& entries, std::string_view mountpoint) noexcept -> std::optional<MTabEntry> {
    return utils::find_optional(entries | std::ranges::views::reverse, mountpoint, &MTabEntry::mountpoint);
}

auto mount_option_value(std::string_view options, std::string_view key) noexcept -> std::optional<std::string_view> {
    return utils::value_of(utils::make_split_view(options, ','), key);
}

auto parse_mtab(std::string_view root_mountpoint, std::string_view mtab_path) noexcept -> std::optional<std::vector<MTabEntry>> {
    // mountpoint and mtab paths cannot be empty
    if (root_mountpoint.empty() || mtab_path.empty()) {
        return std::nullopt;
    }

    const auto file_content = file_utils::read_whole_file(mtab_path);
    if (file_content.empty()) {
        return std::nullopt;
    }
    return mtab::parse_mtab_content(file_content, root_mountpoint);
}

}  // namespace gucc::mtab
