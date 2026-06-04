#include "gucc/zfs_query.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <charconv>  // for from_chars

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace gucc::fs::zfs_query::detail {

auto parse_zfs_size(std::string_view size_str) noexcept -> std::uint64_t {
    if (size_str.empty() || size_str == "-"sv) {
        return 0;
    }

    double value{0};
    const auto* const end_ptr = size_str.data() + size_str.size();
    auto result               = std::from_chars(size_str.data(), end_ptr, value);
    if (result.ec != std::errc{}) {
        return 0;
    }

    std::uint64_t multiplier{1};
    if (result.ptr < end_ptr) {
        const char suffix = static_cast<char>(std::toupper(static_cast<unsigned char>(*result.ptr)));
        switch (suffix) {
        case 'T':
            multiplier = 1024ULL * 1024 * 1024 * 1024;
            break;
        case 'G':
            multiplier = 1024ULL * 1024 * 1024;
            break;
        case 'M':
            multiplier = 1024ULL * 1024;
            break;
        case 'K':
            multiplier = 1024ULL;
            break;
        default:
            break;
        }
    }

    return static_cast<std::uint64_t>(value * static_cast<double>(multiplier));
}

auto split_tsv(std::string_view line) noexcept -> std::vector<std::string> {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const auto pos = line.find('\t', start);
        if (pos == std::string_view::npos) {
            out.emplace_back(line.substr(start));
            return out;
        }
        out.emplace_back(line.substr(start, pos - start));
        start = pos + 1;
    }
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

}  // namespace gucc::fs::zfs_query::detail

namespace {

/// Parse percentage string from `-Hp` output
auto parse_percentage(std::string_view pct_str) noexcept -> std::uint32_t {
    if (pct_str.empty() || pct_str == "-"sv) {
        return 0;
    }
    return gucc::utils::parse_uint<std::uint32_t>(pct_str).value_or(0);
}

}  // namespace

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

    const auto& output = utils::exec(
        R"(zpool list -Hp -o name,size,alloc,free,frag,cap,health,altroot 2>/dev/null)"sv);

    if (output.empty()) {
        return {};
    }

    std::vector<ZfsPoolInfo> pools{};
    auto lines = utils::make_multiline(output);

    for (const auto& line : lines) {
        if (line.empty()) {
            continue;
        }

        auto fields = zfs_detail::split_tsv(line);
        if (fields.size() < 7) {
            continue;
        }

        ZfsPoolInfo pool{};
        pool.name          = std::move(fields[0]);
        pool.size          = zfs_detail::parse_zfs_size(fields[1]);
        pool.allocated     = zfs_detail::parse_zfs_size(fields[2]);
        pool.free          = zfs_detail::parse_zfs_size(fields[3]);
        pool.fragmentation = parse_percentage(fields[4]);
        pool.capacity      = parse_percentage(fields[5]);
        pool.health        = string_to_zfs_pool_health(fields[6]);

        if (fields.size() > 7 && fields[7] != "-"sv) {
            pool.altroot = std::move(fields[7]);
        }

        // Get devices from zpool status
        const auto& status_output = utils::exec(
            fmt::format(FMT_COMPILE("zpool status -PL '{}' 2>/dev/null | awk '{{print $1}}' | grep '^/'"), pool.name));
        if (!status_output.empty()) {
            pool.devices = utils::make_multiline(status_output);
        }

        // Check if bootfs is set
        const auto& bootfs = utils::exec(
            fmt::format(FMT_COMPILE("zpool get -Hp bootfs '{}' 2>/dev/null | awk '{{print $3}}'"), pool.name));
        pool.bootfs_set = !bootfs.empty() && bootfs.find('-') == std::string::npos;

        pools.emplace_back(std::move(pool));
    }

    return pools;
}

auto get_zfs_pool_info(std::string_view pool_name) noexcept -> std::optional<ZfsPoolInfo> {
    if (!is_zfs_available() || pool_name.empty()) {
        return std::nullopt;
    }

    auto pools = list_zfs_pools();
    auto it    = std::ranges::find(pools, pool_name, &ZfsPoolInfo::name);
    if (it != pools.end()) {
        return std::make_optional(std::move(*it));
    }

    return std::nullopt;
}

auto list_zfs_datasets(std::string_view pool_name) noexcept -> std::vector<ZfsDatasetInfo> {
    if (!is_zfs_available()) {
        return {};
    }

    std::string cmd = "zfs list -Hp -o name,mountpoint,used,avail,refer,compression,encryption,recordsize,type,mounted"s;
    if (!pool_name.empty()) {
        cmd += fmt::format(FMT_COMPILE(" -r '{}'"), pool_name);
    }
    cmd += " 2>/dev/null"sv;

    const auto& output = utils::exec(cmd);
    if (output.empty()) {
        return {};
    }

    std::vector<ZfsDatasetInfo> datasets{};
    auto lines = utils::make_multiline(output);

    for (const auto& line : lines) {
        if (line.empty()) {
            continue;
        }
        if (auto ds = zfs_detail::parse_dataset_fields(zfs_detail::split_tsv(line))) {
            datasets.emplace_back(*std::move(ds));
        }
    }

    return datasets;
}

auto get_zfs_dataset_info(std::string_view dataset_path) noexcept -> std::optional<ZfsDatasetInfo> {
    if (!is_zfs_available() || dataset_path.empty()) {
        return std::nullopt;
    }

    const auto& output = utils::exec(
        fmt::format(FMT_COMPILE("zfs list -Hp -o name,mountpoint,used,avail,refer,compression,encryption,recordsize,type,mounted '{}' 2>/dev/null"), dataset_path));

    if (output.empty()) {
        return std::nullopt;
    }

    return zfs_detail::parse_dataset_fields(zfs_detail::split_tsv(output));
}

auto is_zfs_available() noexcept -> bool {
    // Check if zfs command exists
    return utils::exec_checked("command -v zfs >/dev/null 2>&1 && command -v zpool >/dev/null 2>&1");
}

}  // namespace gucc::fs
