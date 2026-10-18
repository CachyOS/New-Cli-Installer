#include "gucc/btrfs_query.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/ranges_utils.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>  // for find
#include <ranges>     // for ranges::*

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::fs::btrfs_query::detail {

auto parse_subvolume_list(std::string_view output, std::string_view filter_prefix) noexcept
    -> std::vector<BtrfsSubvolumeInfo> {
    using gucc::utils::extract_after;
    using gucc::utils::parse_uint;

    constexpr auto parse_line = [](std::string_view line) -> std::optional<BtrfsSubvolumeInfo> {
        if (line.empty()) {
            return std::nullopt;
        }

        BtrfsSubvolumeInfo snap{};
        std::string_view sv{line};

        if (auto val = extract_after(sv, "ID "sv)) {
            snap.id = parse_uint<std::uint64_t>(*val).value_or(0);
        }
        if (auto val = extract_after(sv, "gen "sv)) {
            snap.gen = parse_uint<std::uint64_t>(*val).value_or(0);
        }
        if (auto val = extract_after(sv, "parent "sv)) {
            snap.parent_id = parse_uint<std::uint64_t>(*val).value_or(0);
        }
        if (auto val = extract_after(sv, "top level "sv)) {
            snap.top_level = parse_uint<std::uint64_t>(*val).value_or(0);
        }

        if (auto val = extract_after(sv, "parent_uuid "sv); val && *val != "-"sv) {
            snap.parent_uuid = std::string{*val};
        }
        if (auto val = extract_after(sv, " uuid "sv); val && *val != "-"sv) {
            snap.uuid = std::string{*val};
        }

        if (auto val = extract_after(sv, "path "sv, '\0')) {
            snap.path = std::string{*val};
        }

        if (snap.path.empty()) {
            return std::nullopt;
        }
        return snap;
    };

    const auto matches_prefix = [filter_prefix](const BtrfsSubvolumeInfo& info) {
        return filter_prefix.empty() || info.path.starts_with(filter_prefix);
    };

    return utils::make_split_view(output)
        | std::ranges::views::transform(parse_line)
        | std::ranges::views::join
        | std::ranges::views::filter(matches_prefix)
        | std::ranges::to<std::vector<BtrfsSubvolumeInfo>>();
}

}  // namespace gucc::fs::btrfs_query::detail

namespace {

using Info = gucc::fs::BtrfsFilesystemInfo;

constexpr auto kLabelKey = "Label:"sv;
constexpr auto kUuidKey  = "uuid:"sv;

struct UsageField {
    std::string_view key;
    std::uint64_t Info::* member;
};

struct ProfileField {
    std::string_view prefix;
    std::string Info::* member;
};

// TODO(vnepogodin): such as nasty workaround
constexpr UsageField usage_fields[] = {
    {.key = "Device size"sv, .member = &Info::total_size},
    {.key = "Used"sv, .member = &Info::used},
    {.key = "Free (estimated)"sv, .member = &Info::free},
};

constexpr ProfileField profile_fields[] = {
    {.prefix = "Data,"sv, .member = &Info::data_profile},
    {.prefix = "Metadata,"sv, .member = &Info::metadata_profile},
};

void parse_usage_line(Info& info, std::string_view line) noexcept {
    using gucc::utils::trim;

    const auto split = gucc::utils::split_once(line, ':');
    if (!split) {
        return;
    }
    const auto key   = trim(split->first);
    const auto value = trim(split->second);

    if (const auto* field = gucc::utils::find_ptr(usage_fields, key, &UsageField::key)) {
        info.*field->member = gucc::utils::parse_uint_exact<std::uint64_t>(gucc::utils::first_field(value)).value_or(0);
        return;
    }
    for (const auto& [prefix, member] : profile_fields) {
        if (key.starts_with(prefix)) {
            info.*member = std::string{key.substr(prefix.size())};
            return;
        }
    }
}

// TODO(vnepogodin): even worse.. but let it be
template <bool kLast>
constexpr auto find_key(std::string_view line, std::string_view key) noexcept -> std::size_t {
    const auto at_boundary = [line](std::size_t pos) { return pos == 0 || " \t"sv.contains(line[pos - 1]); };
    if constexpr (kLast) {
        for (auto pos = line.rfind(key); pos != std::string_view::npos; pos = pos == 0 ? std::string_view::npos : line.rfind(key, pos - 1)) {
            if (at_boundary(pos)) {
                return pos;
            }
        }
    } else {
        for (auto pos = line.find(key); pos != std::string_view::npos; pos = line.find(key, pos + 1)) {
            if (at_boundary(pos)) {
                return pos;
            }
        }
    }
    return std::string_view::npos;
}

auto parse_show_label(std::string_view line) noexcept -> std::optional<std::string> {
    const auto key = find_key<false>(line, kLabelKey);
    if (key == std::string_view::npos) {
        return std::nullopt;
    }
    auto value = line.substr(key + kLabelKey.size());
    value      = gucc::utils::ltrim(value.substr(0, find_key<true>(value, kUuidKey)));
    if (!value.starts_with('\'')) {
        return std::nullopt;
    }
    value.remove_prefix(1);
    const auto close = value.rfind('\'');
    if (close == std::string_view::npos) {
        return std::nullopt;
    }
    return std::string{value.substr(0, close)};
}

auto parse_show_uuid(std::string_view line) noexcept -> std::optional<std::string> {
    const auto key = find_key<true>(line, kUuidKey);
    if (key == std::string_view::npos) {
        return std::nullopt;
    }
    const auto uuid = gucc::utils::first_field(line.substr(key + kUuidKey.size()));
    return uuid.empty() ? std::nullopt : std::optional{std::string{uuid}};
}

}  // namespace

namespace gucc::fs::btrfs_query::detail {

auto parse_btrfs_usage(std::string_view output) noexcept -> std::optional<BtrfsFilesystemInfo> {
    BtrfsFilesystemInfo info{};
    for (const auto line : utils::make_split_view(output)) {
        parse_usage_line(info, line);
    }
    // every btrfs filesystem has a nonzero device size??
    if (info.total_size == 0) {
        return std::nullopt;
    }
    return info;
}

auto parse_btrfs_show(std::string_view output) noexcept -> BtrfsShowInfo {
    BtrfsShowInfo show{};
    for (const auto line : utils::make_split_view(output)) {
        if (!show.label) {
            show.label = parse_show_label(line);
        }
        if (!show.uuid) {
            show.uuid = parse_show_uuid(line);
        }
    }
    return show;
}

auto parse_subvolume_paths(std::string_view output, std::string_view filter_prefix) noexcept -> std::vector<BtrfsSubvolume> {
    constexpr auto extract_path = [](std::string_view line) -> std::optional<std::string_view> {
        return utils::extract_after(line, "path "sv, '\0').and_then([](std::string_view path) {
            return path.empty() ? std::nullopt : std::optional{path};
        });
    };

    const auto matches_prefix = [filter_prefix](std::string_view path) {
        return filter_prefix.empty() || path.starts_with(filter_prefix);
    };

    constexpr auto to_subvolume = [](std::string_view path) -> BtrfsSubvolume {
        BtrfsSubvolume subvol{};
        subvol.subvolume  = std::string{path};
        subvol.mountpoint = fmt::format(FMT_COMPILE("/{0}"), subvol.subvolume);
        return subvol;
    };

    return utils::make_split_view(output)
        | std::ranges::views::transform(extract_path)
        | std::ranges::views::join
        | std::ranges::views::filter(matches_prefix)
        | std::ranges::views::transform(to_subvolume)
        | std::ranges::to<std::vector<BtrfsSubvolume>>();
}

}  // namespace gucc::fs::btrfs_query::detail

namespace gucc::fs {

namespace btrfs_query_detail = btrfs_query::detail;

auto list_btrfs_subvolumes(std::string_view mountpoint, std::string_view filter_prefix) noexcept -> std::vector<BtrfsSubvolume> {
    if (mountpoint.empty()) {
        return {};
    }

    const auto& output = utils::exec(fmt::format(FMT_COMPILE("btrfs subvolume list -o '{}' 2>/dev/null"), mountpoint));
    if (output.empty()) {
        return {};
    }

    return btrfs_query_detail::parse_subvolume_paths(output, filter_prefix);
}

auto list_btrfs_snapshots(std::string_view mountpoint, std::string_view filter_prefix) noexcept -> std::vector<BtrfsSubvolumeInfo> {
    if (mountpoint.empty()) {
        return {};
    }

    auto all_subvols = list_btrfs_subvolumes_detailed(mountpoint, filter_prefix);

    return all_subvols
        | std::ranges::views::filter([](const auto& subvol) { return subvol.parent_uuid.has_value(); })
        | std::ranges::to<std::vector<BtrfsSubvolumeInfo>>();
}

auto list_btrfs_subvolumes_detailed(std::string_view mountpoint, std::string_view filter_prefix) noexcept -> std::vector<BtrfsSubvolumeInfo> {
    if (mountpoint.empty()) {
        return {};
    }

    const auto& output = utils::exec(fmt::format(FMT_COMPILE("btrfs subvolume list -puq '{}' 2>/dev/null"), mountpoint));
    if (output.empty()) {
        return {};
    }

    return btrfs_query_detail::parse_subvolume_list(output, filter_prefix);
}

auto get_btrfs_info(std::string_view mountpoint_or_device) noexcept -> std::optional<BtrfsFilesystemInfo> {
    if (mountpoint_or_device.empty()) {
        return std::nullopt;
    }

    auto usage = utils::query_output(fmt::format(FMT_COMPILE("btrfs filesystem usage -b '{}' 2>/dev/null"), mountpoint_or_device))
                     .and_then(btrfs_query_detail::parse_btrfs_usage);
    if (!usage) {
        return std::nullopt;
    }
    const auto show_output = utils::query_output(fmt::format(FMT_COMPILE("btrfs filesystem show '{}' 2>/dev/null"), mountpoint_or_device));
    if (!show_output) {
        return std::nullopt;
    }

    auto info   = std::move(*usage);
    auto show   = btrfs_query_detail::parse_btrfs_show(*show_output);
    info.device = std::string{mountpoint_or_device};
    info.uuid   = std::move(show.uuid);
    info.label  = std::move(show.label);

    return info;
}

auto is_btrfs(std::string_view path) noexcept -> bool {
    if (path.empty()) {
        return false;
    }

    const auto& output = utils::exec(fmt::format(FMT_COMPILE("stat -f -c %T '{}' 2>/dev/null"), path));
    return output.contains("btrfs");
}

}  // namespace gucc::fs
