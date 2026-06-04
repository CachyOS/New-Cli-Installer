#include "gucc/btrfs_query.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>  // for for_each, transform
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
        | std::ranges::views::filter(&std::optional<BtrfsSubvolumeInfo>::has_value)
        | std::ranges::views::transform([](auto&& opt) -> BtrfsSubvolumeInfo { return std::move(*opt); })
        | std::ranges::views::filter(matches_prefix)
        | std::ranges::to<std::vector<BtrfsSubvolumeInfo>>();
}

}  // namespace gucc::fs::btrfs_query::detail

namespace {

using Info = gucc::fs::BtrfsFilesystemInfo;

void assign_data_profile(Info& info, std::string_view val) { info.data_profile = std::string{val}; }
void assign_metadata_profile(Info& info, std::string_view val) { info.metadata_profile = std::string{val}; }

using StringSetter = void (*)(Info&, std::string_view);

struct DFProfileField {
    std::string_view marker;
    StringSetter assign;
};

constexpr DFProfileField df_profile_fields[] = {
    {"Data,"sv, assign_data_profile},
    {"Metadata,"sv, assign_metadata_profile},
};

auto parse_btrfs_df_output(std::string_view output, Info& info) -> bool {
    std::ranges::for_each(gucc::utils::make_split_view(output), [&](std::string_view line) {
        const auto it = std::ranges::find_if(df_profile_fields,
            [&line](const auto& desc) { return line.contains(desc.marker); });
        if (it != std::ranges::end(df_profile_fields)) {
            const auto start = it->marker.size();
            if (const auto colon = line.find(':', start); colon != std::string_view::npos) {
                it->assign(info, gucc::utils::trim(line.substr(start, colon - start)));
            }
        }
    });
    return true;
}

void assign_total_size(Info& info, std::uint64_t val) { info.total_size = val; }
void assign_used(Info& info, std::uint64_t val) { info.used = val; }
void assign_free(Info& info, std::uint64_t val) { info.free = val; }

using U64Setter = void (*)(Info&, std::uint64_t);

struct UsageField {
    std::string_view prefix;
    U64Setter assign;
};

constexpr UsageField usage_fields[] = {
    {"Device size:"sv, assign_total_size},
    {"    Used:"sv, assign_used},
    {"Free (estimated):"sv, assign_free},
};

void assign_label(Info& info, std::string_view line) {
    const auto start = line.find('\'');
    if (start == std::string_view::npos) {
        return;
    }
    const auto end = line.find('\'', start + 1);
    if (end == std::string_view::npos) {
        return;
    }
    info.label = std::string{line.substr(start + 1, end - start - 1)};
}

void assign_uuid(Info& info, std::string_view line) {
    using gucc::utils::extract_after;
    using gucc::utils::rtrim;
    if (auto val = extract_after(line, "uuid: "sv)) {
        info.uuid = std::string{rtrim(*val)};
    }
}

using ShowSetter = void (*)(Info&, std::string_view);

struct ShowField {
    std::string_view marker;
    ShowSetter assign;
};

constexpr ShowField show_fields[] = {
    {"Label:"sv, assign_label},
    {"uuid: "sv, assign_uuid},
};

}  // namespace

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

    constexpr auto extract_path = [](std::string_view line) -> std::optional<std::string_view> {
        auto pos = line.find("path "sv);
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }
        auto subvol_path = line.substr(pos + 5);
        if (subvol_path.empty()) {
            return std::nullopt;
        }
        return subvol_path;
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
        | std::ranges::views::filter(&std::optional<std::string_view>::has_value)
        | std::ranges::views::transform([](auto&& opt) { return *opt; })
        | std::ranges::views::filter(matches_prefix)
        | std::ranges::views::transform(to_subvolume)
        | std::ranges::to<std::vector<BtrfsSubvolume>>();
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
    using utils::extract_after;
    using utils::ltrim;
    using utils::parse_uint;

    if (mountpoint_or_device.empty()) {
        return std::nullopt;
    }

    BtrfsFilesystemInfo info{};
    info.device = std::string{mountpoint_or_device};

    const auto& usage_output = utils::exec(fmt::format(FMT_COMPILE("btrfs filesystem usage -b '{}' 2>/dev/null"), mountpoint_or_device));
    if (!usage_output.empty()) {
        std::ranges::for_each(utils::make_split_view(usage_output), [&](std::string_view line) {
            for (const auto& field : usage_fields) {
                if (auto val = extract_after(line, field.prefix, '\n')) {
                    field.assign(info, parse_uint<std::uint64_t>(ltrim(*val)).value_or(0));
                    break;
                }
            }
        });
    }

    const auto& df_output = utils::exec(fmt::format(FMT_COMPILE("btrfs filesystem df '{}' 2>/dev/null"), mountpoint_or_device));
    if (!df_output.empty()) {
        parse_btrfs_df_output(df_output, info);
    }

    const auto& show_output = utils::exec(fmt::format(FMT_COMPILE("btrfs filesystem show '{}' 2>/dev/null"), mountpoint_or_device));
    if (!show_output.empty()) {
        std::ranges::for_each(utils::make_split_view(show_output), [&](std::string_view line) {
            for (const auto& field : show_fields) {
                if (line.contains(field.marker)) {
                    field.assign(info, line);
                    break;
                }
            }
        });
    }

    return std::make_optional(std::move(info));
}

auto is_btrfs(std::string_view path) noexcept -> bool {
    if (path.empty()) {
        return false;
    }

    const auto& output = utils::exec(fmt::format(FMT_COMPILE("stat -f -c %T '{}' 2>/dev/null"), path));
    return output.contains("btrfs");
}

}  // namespace gucc::fs
