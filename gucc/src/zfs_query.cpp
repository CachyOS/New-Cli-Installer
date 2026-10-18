#include "gucc/zfs_query.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/memory.hpp"
#include "gucc/ranges_utils.hpp"
#include "gucc/string_utils.hpp"

#include <charconv>    // for from_chars
#include <functional>  // for less
#include <map>         // for map
#include <optional>    // for optional
#include <ranges>      // for ranges::*
#include <utility>     // for forward, move, pair

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace {

// NOLINTNEXTLINE
using namespace gucc;

constexpr auto kDatasetColumns = "name,mountpoint,used,avail,refer,compression,encryption,recordsize,type,mounted"sv;

constexpr auto pool_header_name(std::string_view line) noexcept -> std::optional<std::string_view> {
    return utils::key_value(utils::trim(line), "pool"sv, ':').and_then([](std::string_view value) {
        const auto name = utils::trim(value);
        return name.empty() ? std::nullopt : std::optional{name};
    });
}

template <std::ranges::input_range Lines>
auto vdev_paths(Lines&& lines) noexcept -> std::vector<std::string> {
    return std::forward<Lines>(lines)
        | std::ranges::views::transform(utils::first_field)
        | std::ranges::views::filter([](std::string_view field) { return field.starts_with('/'); })
        | std::ranges::to<std::vector<std::string>>();
}

}  // namespace

namespace gucc::fs::zfs_query::detail {

auto parse_zfs_size(std::string_view size_str) noexcept -> std::uint64_t {
    double value{0};
    const auto* const end_ptr = size_str.data() + size_str.size();
    const auto result         = std::from_chars(size_str.data(), end_ptr, value);
    if (result.ec != std::errc{}) {
        return 0;
    }
    // unknown suffix, or doesn't exist at all?
    const auto multiplier = result.ptr < end_ptr ? memory::unit_multiplier(*result.ptr).value_or(1) : 1;
    return memory::scale_fractional_bytes(value, multiplier).value_or(0);
}

auto split_tsv(std::string_view line) noexcept -> std::vector<std::string> {
    if (line.empty()) {
        return {std::string{}};
    }
    return utils::split_lines(line, '\t') | std::ranges::to<std::vector<std::string>>();
}

auto parse_dataset_fields(std::vector<std::string> fields) noexcept
    -> std::optional<ZfsDatasetInfo> {
    if (fields.size() < 9) {
        return std::nullopt;
    }

    ZfsDatasetInfo ds{};
    ds.name       = std::move(fields[0]);
    ds.mountpoint = std::move(fields[1]);
    ds.used       = parse_zfs_size(fields[2]);
    ds.available  = parse_zfs_size(fields[3]);
    ds.referenced = parse_zfs_size(fields[4]);

    if (fields[5] != "off"sv && fields[5] != "-"sv) {
        ds.compression = std::move(fields[5]);
    }

    ds.encryption = (fields[6] != "off"sv && fields[6] != "-"sv);

    if (fields[7] != "-"sv) {
        if (auto rs = gucc::utils::parse_uint<std::uint32_t>(fields[7]); rs.value_or(0) > 0) {
            ds.recordsize = rs;
        }
    }

    ds.type = std::move(fields[8]);

    if (fields.size() > 9) {
        ds.mounted = (fields[9] == "yes"sv);
    }

    return std::make_optional(std::move(ds));
}

auto parse_zpool_status_devices(std::string_view output) noexcept -> std::vector<std::string> {
    return vdev_paths(utils::make_split_view(output));
}

auto parse_zpool_status_pools(std::string_view output) noexcept -> PoolMap<std::vector<std::string>> {
    constexpr auto same_block = [](std::string_view /*prev*/, std::string_view next) {
        return !pool_header_name(next).has_value();
    };
    constexpr auto to_entry = [](auto&& block) -> std::optional<std::pair<std::string, std::vector<std::string>>> {
        const auto name = pool_header_name(*std::ranges::begin(block));
        if (!name) {
            return std::nullopt;
        }
        return std::pair{std::string{*name}, vdev_paths(block | std::ranges::views::drop(1))};
    };
    return utils::make_split_view(output)
        | std::ranges::views::chunk_by(same_block)
        | std::ranges::views::transform(to_entry)
        | std::ranges::views::join
        | std::ranges::to<PoolMap<std::vector<std::string>>>();
}

auto parse_zpool_get_values(std::string_view output) noexcept -> PoolMap<std::string> {
    // NAME<TAB>PROPERTY<TAB>VALUE<TAB>SOURCE
    constexpr auto to_entry = [](std::string_view line) {
        return utils::split_fields<3>(line, '\t').transform([](const auto& fields) {
            const auto& [name, property, value] = fields;
            return std::pair{std::string{name}, std::string{value}};
        });
    };
    return utils::make_split_view(output)
        | std::ranges::views::transform(to_entry)
        | std::ranges::views::join
        | std::ranges::to<PoolMap<std::string>>();
}

auto child_dataset_names(const std::vector<ZfsDatasetInfo>& datasets, bool legacy_only) noexcept
    -> std::vector<std::string> {
    return datasets
        | std::ranges::views::filter([legacy_only](const ZfsDatasetInfo& ds) {
              return ds.name.contains('/') && (!legacy_only || (ds.type == "filesystem"sv && ds.mountpoint == "legacy"sv));
          })
        | std::ranges::views::transform(&ZfsDatasetInfo::name)
        | std::ranges::to<std::vector<std::string>>();
}

auto parse_importable_zpools(std::string_view output) noexcept -> std::vector<std::string> {
    return utils::make_split_view(output)
        | std::ranges::views::transform(pool_header_name)
        | std::ranges::views::join
        | std::ranges::to<std::vector<std::string>>();
}

auto parse_percentage(std::string_view pct_str) noexcept -> std::uint32_t {
    return utils::parse_uint<std::uint32_t>(pct_str).value_or(0);
}

auto parse_zpool_list_row(std::string_view line) noexcept -> std::optional<ZfsPoolInfo> {
    auto fields = split_tsv(line);
    if (fields.size() < 7) {
        return std::nullopt;
    }

    ZfsPoolInfo pool{};
    pool.name          = std::move(fields.at(0));
    pool.size          = parse_zfs_size(fields.at(1));
    pool.allocated     = parse_zfs_size(fields.at(2));
    pool.free          = parse_zfs_size(fields.at(3));
    pool.fragmentation = parse_percentage(fields.at(4));
    pool.capacity      = parse_percentage(fields.at(5));
    pool.health        = string_to_zfs_pool_health(fields.at(6));
    if (fields.size() > 7 && fields.at(7) != "-"sv) {
        pool.altroot = std::move(fields.at(7));
    }
    return pool;
}

auto parse_zpool_list(std::string_view output, const PoolMap<std::vector<std::string>>& devices, const PoolMap<std::string>& bootfs) noexcept
    -> std::vector<ZfsPoolInfo> {
    const auto with_status = [&](ZfsPoolInfo pool) -> ZfsPoolInfo {
        if (const auto it = devices.find(pool.name); it != devices.end()) {
            pool.devices = it->second;
        }
        // "-" when unset
        const auto bootfs_it = bootfs.find(pool.name);
        pool.bootfs_set      = bootfs_it != bootfs.end() && !bootfs_it->second.empty() && bootfs_it->second != "-"sv;
        return pool;
    };

    return utils::make_split_view(output)
        | std::ranges::views::transform(parse_zpool_list_row)
        | std::ranges::views::join
        | std::ranges::views::transform(with_status)
        | std::ranges::to<std::vector<ZfsPoolInfo>>();
}

}  // namespace gucc::fs::zfs_query::detail

namespace gucc::fs {

namespace zfs_detail = zfs_query::detail;

auto zfs_pool_health_to_string(ZfsPoolHealth health) noexcept -> std::string_view {
    switch (health) {
    case ZfsPoolHealth::Online:
        return "ONLINE"sv;
    case ZfsPoolHealth::Degraded:
        return "DEGRADED"sv;
    case ZfsPoolHealth::Faulted:
        return "FAULTED"sv;
    case ZfsPoolHealth::Offline:
        return "OFFLINE"sv;
    case ZfsPoolHealth::Unavailable:
        return "UNAVAIL"sv;
    case ZfsPoolHealth::Suspended:
        return "SUSPENDED"sv;
    case ZfsPoolHealth::Unknown:
    default:
        return "UNKNOWN"sv;
    }
}

auto string_to_zfs_pool_health(std::string_view health_str) noexcept -> ZfsPoolHealth {
    // ZFS uses uppercase for health status
    if (health_str == "ONLINE"sv) {
        return ZfsPoolHealth::Online;
    } else if (health_str == "DEGRADED"sv) {
        return ZfsPoolHealth::Degraded;
    } else if (health_str == "FAULTED"sv) {
        return ZfsPoolHealth::Faulted;
    } else if (health_str == "OFFLINE"sv) {
        return ZfsPoolHealth::Offline;
    } else if (health_str == "UNAVAIL"sv) {
        return ZfsPoolHealth::Unavailable;
    } else if (health_str == "SUSPENDED"sv) {
        return ZfsPoolHealth::Suspended;
    }
    return ZfsPoolHealth::Unknown;
}

auto list_zfs_pools() noexcept -> std::vector<ZfsPoolInfo> {
    if (!is_zfs_available()) {
        return {};
    }

    const auto& output = utils::exec(R"(zpool list -Hp -o name,size,alloc,free,frag,cap,health,altroot 2>/dev/null)"sv);

    if (output.empty()) {
        return {};
    }

    // one query per property for all pools
    const auto devices = zfs_detail::parse_zpool_status_pools(utils::exec("zpool status -PL 2>/dev/null"sv));
    const auto bootfs  = zfs_detail::parse_zpool_get_values(utils::exec("zpool get -Hp bootfs 2>/dev/null"sv));

    return zfs_detail::parse_zpool_list(output, devices, bootfs);
}

auto get_zfs_pool_info(std::string_view pool_name) noexcept -> std::optional<ZfsPoolInfo> {
    if (pool_name.empty()) {
        return std::nullopt;
    }
    return utils::find_optional(list_zfs_pools(), pool_name, &ZfsPoolInfo::name);
}

auto list_zfs_datasets(std::string_view pool_name) noexcept -> std::vector<ZfsDatasetInfo> {
    if (!is_zfs_available()) {
        return {};
    }

    const auto& cmd    = pool_name.empty()
        ? fmt::format(FMT_COMPILE("zfs list -Hp -o {} 2>/dev/null"), kDatasetColumns)
        : fmt::format(FMT_COMPILE("zfs list -Hp -o {} -r '{}' 2>/dev/null"), kDatasetColumns, pool_name);
    const auto& output = utils::exec(cmd);
    return utils::make_split_view(output)
        | std::ranges::views::transform([](std::string_view line) { return zfs_detail::parse_dataset_fields(zfs_detail::split_tsv(line)); })
        | std::ranges::views::join
        | std::ranges::to<std::vector<ZfsDatasetInfo>>();
}

auto get_zfs_dataset_info(std::string_view dataset_path) noexcept -> std::optional<ZfsDatasetInfo> {
    if (!is_zfs_available() || dataset_path.empty()) {
        return std::nullopt;
    }

    const auto& output = utils::exec(fmt::format(FMT_COMPILE("zfs list -Hp -o {} '{}' 2>/dev/null"), kDatasetColumns, dataset_path));

    if (output.empty()) {
        return std::nullopt;
    }

    return zfs_detail::parse_dataset_fields(zfs_detail::split_tsv(output));
}

auto is_zfs_available() noexcept -> bool {
    // Check if zfs command exists
    return utils::query_checked("command -v zfs >/dev/null 2>&1 && command -v zpool >/dev/null 2>&1"sv);
}

}  // namespace gucc::fs
