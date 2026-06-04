#include "gucc/zfs_query.hpp"
#ifndef COS_BUILD_STATIC
#include "gucc/logger.hpp"
#endif

#include <cstdlib>
#include <string>
#include <string_view>

#include <fmt/color.h>
#include <fmt/format.h>

#include <spdlog/async.h>                     // for create_async
#include <spdlog/common.h>                    // for debug
#include <spdlog/sinks/stdout_color_sinks.h>  // for stdout_color_sink_mt
#include <spdlog/spdlog.h>                    // for set_default_logger, set_level

using namespace std::string_view_literals;

void print_usage(const char* program_name) {
    fmt::println(stderr, "Usage: {} [OPTIONS] [POOL]", program_name);
    fmt::println(stderr, "\nQuery ZFS pools and datasets.");
    fmt::println(stderr, "\nOptions:");
    fmt::println(stderr, "  -h, --help       Show this help message");
    fmt::println(stderr, "  -p, --pools      List all imported pools (default if no pool specified)");
    fmt::println(stderr, "  -d, --datasets   List datasets in pool");
    fmt::println(stderr, "  -a, --all        Show all pools and their datasets");
    fmt::println(stderr, "\nExamples:");
    fmt::println(stderr, "  {}                List all pools", program_name);
    fmt::println(stderr, "  {} zroot          Show pool info for zroot", program_name);
    fmt::println(stderr, "  {} -d zroot       List datasets in zroot", program_name);
    fmt::println(stderr, "  {} -a             List all pools and datasets", program_name);
}

std::string format_size(std::uint64_t bytes) {
    constexpr std::uint64_t KiB = 1024ULL;
    constexpr std::uint64_t MiB = KiB * 1024;
    constexpr std::uint64_t GiB = MiB * 1024;
    constexpr std::uint64_t TiB = GiB * 1024;

    if (bytes >= TiB) {
        return fmt::format("{:.1f}T", static_cast<double>(bytes) / TiB);
    } else if (bytes >= GiB) {
        return fmt::format("{:.1f}G", static_cast<double>(bytes) / GiB);
    } else if (bytes >= MiB) {
        return fmt::format("{:.0f}M", static_cast<double>(bytes) / MiB);
    } else if (bytes >= KiB) {
        return fmt::format("{:.0f}K", static_cast<double>(bytes) / KiB);
    }
    return fmt::format("{}B", bytes);
}

void print_pool_info(const gucc::fs::ZfsPoolInfo& pool) {
    // Determine health color
    fmt::text_style health_style = fmt::emphasis::bold;
    switch (pool.health) {
    case gucc::fs::ZfsPoolHealth::Online:
        health_style = fmt::fg(fmt::color::green) | fmt::emphasis::bold;
        break;
    case gucc::fs::ZfsPoolHealth::Degraded:
        health_style = fmt::fg(fmt::color::yellow) | fmt::emphasis::bold;
        break;
    case gucc::fs::ZfsPoolHealth::Faulted:
    case gucc::fs::ZfsPoolHealth::Unavailable:
        health_style = fmt::fg(fmt::color::red) | fmt::emphasis::bold;
        break;
    default:
        break;
    }

    fmt::print(fmt::emphasis::bold, "{}\n", pool.name);
    fmt::print("  Health:       ");
    fmt::print(health_style, "{}\n", gucc::fs::zfs_pool_health_to_string(pool.health));
    fmt::println("  Size:         {}", format_size(pool.size));
    fmt::println("  Allocated:    {} ({}%)", format_size(pool.allocated), pool.capacity);
    fmt::println("  Free:         {}", format_size(pool.free));
    fmt::println("  Fragmentation: {}%", pool.fragmentation);

    if (pool.altroot) {
        fmt::println("  Altroot:      {}", *pool.altroot);
    }
    fmt::println("  Bootable:     {}", pool.bootfs_set ? "yes" : "no");

    if (!pool.devices.empty()) {
        fmt::println("  Devices:");
        for (const auto& dev : pool.devices) {
            fmt::println("    - {}", dev);
        }
    }
    fmt::println("");
}

void list_pools() {
    auto pools = gucc::fs::list_zfs_pools();
    if (pools.empty()) {
        fmt::println("No ZFS pools found");
        return;
    }

    fmt::print(fmt::emphasis::bold, "ZFS Pools:\n\n");
    fmt::println("{:15} {:10} {:10} {:10} {:8} {:8} {}",
        "NAME", "SIZE", "ALLOC", "FREE", "CAP", "FRAG", "HEALTH");
    fmt::println("{:->75}", "");

    for (const auto& pool : pools) {
        // Determine health color
        auto health_style = fmt::text_style{};
        switch (pool.health) {
        case gucc::fs::ZfsPoolHealth::Online:
            health_style = fmt::fg(fmt::color::green);
            break;
        case gucc::fs::ZfsPoolHealth::Degraded:
            health_style = fmt::fg(fmt::color::yellow);
            break;
        case gucc::fs::ZfsPoolHealth::Faulted:
        case gucc::fs::ZfsPoolHealth::Unavailable:
            health_style = fmt::fg(fmt::color::red);
            break;
        default:
            break;
        }

        fmt::print("{:15} {:10} {:10} {:10} {:7}% {:7}% ",
            pool.name,
            format_size(pool.size),
            format_size(pool.allocated),
            format_size(pool.free),
            pool.capacity,
            pool.fragmentation);
        fmt::print(health_style, "{}\n", gucc::fs::zfs_pool_health_to_string(pool.health));
    }
}

void show_pool_info(std::string_view pool_name) {
    auto pool = gucc::fs::get_zfs_pool_info(pool_name);
    if (!pool) {
        fmt::println(stderr, "Pool not found: {}", pool_name);
        return;
    }

    print_pool_info(*pool);
}

void list_datasets(std::string_view pool_name) {
    auto datasets = gucc::fs::list_zfs_datasets(pool_name);
    if (datasets.empty()) {
        if (pool_name.empty()) {
            fmt::println("No ZFS datasets found");
        } else {
            fmt::println("No datasets found in pool: {}", pool_name);
        }
        return;
    }

    fmt::print(fmt::emphasis::bold, "ZFS Datasets");
    if (!pool_name.empty()) {
        fmt::print(" in {}", pool_name);
    }
    fmt::println(":\n");

    fmt::println("{:30} {:15} {:10} {:10} {:10} {}",
        "NAME", "MOUNTPOINT", "USED", "AVAIL", "COMPRESS", "ENCRYPT");
    fmt::println("{:->95}", "");

    for (const auto& ds : datasets) {
        std::string compress_str = ds.compression.value_or("-");
        std::string encrypt_str  = ds.encryption ? "yes" : "-";

        // Truncate long names
        std::string name_str = ds.name;
        if (name_str.size() > 29) {
            name_str = "..." + name_str.substr(name_str.size() - 26);
        }

        std::string mount_str = ds.mountpoint;
        if (mount_str.size() > 14) {
            mount_str = mount_str.substr(0, 11) + "...";
        }

        fmt::println("{:30} {:15} {:10} {:10} {:10} {}",
            name_str,
            mount_str,
            format_size(ds.used),
            format_size(ds.available),
            compress_str,
            encrypt_str);
    }
}

void list_all() {
    auto pools = gucc::fs::list_zfs_pools();
    if (pools.empty()) {
        fmt::println("No ZFS pools found");
        return;
    }

    for (const auto& pool : pools) {
        print_pool_info(pool);

        fmt::println("  Datasets:");
        auto datasets = gucc::fs::list_zfs_datasets(pool.name);
        if (datasets.empty()) {
            fmt::println("    (none)");
        } else {
            for (const auto& ds : datasets) {
                std::string mount_str = ds.mountpoint.empty() ? "-" : ds.mountpoint;
                fmt::println("    {:30} {:15} {}",
                    ds.name, mount_str, format_size(ds.used));
            }
        }
        fmt::println("");
    }
}

int main(int argc, char* argv[]) {
    bool list_pools_only    = false;
    bool list_datasets_flag = false;
    bool show_all           = false;
    std::string pool_name;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "-h"sv || arg == "--help"sv) {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-p"sv || arg == "--pools"sv) {
            list_pools_only = true;
        } else if (arg == "-d"sv || arg == "--datasets"sv) {
            list_datasets_flag = true;
        } else if (arg == "-a"sv || arg == "--all"sv) {
            show_all = true;
        } else if (arg.starts_with('-')) {
            fmt::println(stderr, "Unknown option: {}", arg);
            print_usage(argv[0]);
            return 1;
        } else {
            pool_name = std::string{arg};
        }
    }

    // Initialize logger.
    auto logger = spdlog::create_async<spdlog::sinks::stdout_color_sink_mt>("cachyos_logger");
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%r][%^---%L---%$] %v");
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_every(std::chrono::seconds(5));

#ifndef COS_BUILD_STATIC
    // Set gucc logger.
    gucc::logger::set_logger(logger);
#endif

    // Check if ZFS is available
    if (!gucc::fs::is_zfs_available()) {
        fmt::println(stderr, "ZFS is not available on this system");
        return 1;
    }

    // Execute requested action
    if (show_all) {
        list_all();
    } else if (list_datasets_flag) {
        list_datasets(pool_name);
    } else if (pool_name.empty() || list_pools_only) {
        list_pools();
    } else {
        show_pool_info(pool_name);
    }

    return 0;
}
