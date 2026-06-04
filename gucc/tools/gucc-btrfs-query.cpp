#include "gucc/btrfs_query.hpp"
#ifndef COS_BUILD_STATIC
#include "gucc/logger.hpp"
#endif

#include <algorithm>    // for for_each, find, contains
#include <array>        // for array
#include <cstdlib>      // for exit
#include <ranges>       // for ranges::*
#include <span>         // for span
#include <string>       // for string
#include <string_view>  // for string_view

#include <fmt/color.h>
#include <fmt/format.h>

#include <spdlog/async.h>                     // for create_async
#include <spdlog/common.h>                    // for debug
#include <spdlog/sinks/stdout_color_sinks.h>  // for stdout_color_sink_mt
#include <spdlog/spdlog.h>                    // for set_default_logger, set_level

using namespace std::string_view_literals;

auto truncate_uuid(const std::optional<std::string>& uuid, std::size_t max_len = 12) -> std::string {
    if (!uuid) {
        return "-";
    }
    return (uuid->size() > max_len) ? (uuid->substr(0, 8) + "...") : *uuid;
}

void print_usage(const char* program_name) {
    fmt::println(stderr, "Usage: {} [OPTIONS] <MOUNTPOINT>", program_name);
    fmt::println(stderr, "\nQuery btrfs subvolumes and snapshots.");
    fmt::println(stderr, "\nOptions:");
    fmt::println(stderr, "  -h, --help       Show this help message");
    fmt::println(stderr, "  -s, --snapshots  List only snapshots (subvols with parent_uuid)");
    fmt::println(stderr, "  -d, --detailed   Show detailed subvolume info (IDs, UUIDs)");
    fmt::println(stderr, "  -i, --info       Show filesystem info (usage, profiles)");
    fmt::println(stderr, "  -a, --all        Show all subvolumes (include docker, etc.)");
    fmt::println(stderr, "\nBy default, only subvolumes starting with '@' are shown.");
    fmt::println(stderr, "\nExamples:");
    fmt::println(stderr, "  {} /             List subvolumes on root", program_name);
    fmt::println(stderr, "  {} -s /          List snapshots on root", program_name);
    fmt::println(stderr, "  {} -a /          List all subvolumes (no filter)", program_name);
    fmt::println(stderr, "  {} -i /          Show btrfs filesystem info", program_name);
}

auto make_filter_suffix(std::string_view filter) -> std::string {
    return filter.empty() ? std::string{} : fmt::format(" (filter: {}*)", filter);
}

template <typename QueryFn, typename EmptyFn, typename HeaderFn, typename RowFn>
void print_table(QueryFn query, EmptyFn on_empty, HeaderFn header, RowFn row) {
    auto items = query();
    if (items.empty()) {
        on_empty();
        return;
    }
    header();
    std::ranges::for_each(items, row);
}

void print_subvolumes(std::string_view mountpoint, std::string_view filter) {
    const auto suffix = make_filter_suffix(filter);
    print_table(
        [&] { return gucc::fs::list_btrfs_subvolumes(mountpoint, filter); },
        [&] { fmt::println("No subvolumes found on {}{}", mountpoint, suffix); },
        [&] {
            fmt::print(fmt::emphasis::bold, "Subvolumes on {}:{}\n", mountpoint, suffix);
            fmt::println("\n{:40} {}", "Subvolume", "Mountpoint");
            fmt::println("{:->60}", "");
        },
        [](const auto& subvol) { fmt::println("{:40} {}", subvol.subvolume, subvol.mountpoint); });
}

void print_detailed_subvolumes(std::string_view mountpoint, std::string_view filter) {
    const auto suffix = make_filter_suffix(filter);
    print_table(
        [&] { return gucc::fs::list_btrfs_subvolumes_detailed(mountpoint, filter); },
        [=] { fmt::println("No subvolumes found on {}", mountpoint); },
        [&] {
            fmt::print(fmt::emphasis::bold, "Detailed subvolumes on {}:{}\n", mountpoint, suffix);
            fmt::println("\n{:8} {:8} {:8} {:40} {}", "ID", "Gen", "Parent", "Path", "UUID");
            fmt::println("{:->100}", "");
        },
        [](const auto& snap) {
            fmt::println("{:<8} {:<8} {:<8} {:40} {}",
                snap.id, snap.gen, snap.parent_id, snap.path, truncate_uuid(snap.uuid));
        });
}

void print_snapshots(std::string_view mountpoint, std::string_view filter) {
    const auto suffix = make_filter_suffix(filter);
    print_table(
        [&] { return gucc::fs::list_btrfs_snapshots(mountpoint, filter); },
        [=] { fmt::println("No snapshots found on {}", mountpoint); },
        [&] {
            fmt::print(fmt::emphasis::bold, "Snapshots on {}:{}\n", mountpoint, suffix);
            fmt::println("\n{:8} {:40} {:12} {}", "ID", "Path", "Parent UUID", "Read-only");
            fmt::println("{:->80}", "");
        },
        [](const auto& snap) {
            fmt::println("{:<8} {:40} {:12} {}",
                snap.id, snap.path, truncate_uuid(snap.parent_uuid),
                snap.is_readonly ? "yes" : "no");
        });
}

void print_filesystem_info(std::string_view mountpoint) {
    if (!gucc::fs::is_btrfs(mountpoint)) {
        fmt::println(stderr, "{} is not a btrfs filesystem", mountpoint);
        return;
    }

    auto info = gucc::fs::get_btrfs_info(mountpoint);
    if (!info) {
        fmt::println(stderr, "Failed to get btrfs info for {}", mountpoint);
        return;
    }

    fmt::print(fmt::emphasis::bold, "Btrfs filesystem info for {}:\n\n", mountpoint);

    if (info->label) {
        fmt::println("  Label:    {}", *info->label);
    }
    if (info->uuid) {
        fmt::println("  UUID:     {}", *info->uuid);
    }

    fmt::println("\n  Space Usage:");
    if (info->total_size > 0) {
        const double used_pct = static_cast<double>(info->used) / static_cast<double>(info->total_size) * 100.0;
        fmt::println("    Total:  {} bytes", info->total_size);
        fmt::println("    Used:   {} bytes ({:.1f}%)", info->used, used_pct);
        fmt::println("    Free:   {} bytes", info->free);
    }

    if (!info->data_profile.empty()) {
        fmt::println("\n  Profiles:");
        fmt::println("    Data:     {}", info->data_profile);
        fmt::println("    Metadata: {}", info->metadata_profile);
    }
}

int main(int argc, char* argv[]) {
    bool show_snapshots = false;
    bool show_detailed  = false;
    bool show_info      = false;
    bool show_all       = false;
    std::string mountpoint;

    struct BoolFlag {
        std::string_view name;
        bool& target;
    };

    BoolFlag bool_flags[] = {
        {"-s"sv, show_snapshots},
        {"--snapshots"sv, show_snapshots},
        {"-d"sv, show_detailed},
        {"--detailed"sv, show_detailed},
        {"-i"sv, show_info},
        {"--info"sv, show_info},
        {"-a"sv, show_all},
        {"--all"sv, show_all},
    };

    constexpr std::string_view help_flags[] = {"-h"sv, "--help"sv};

    const auto args = std::span{argv, static_cast<std::size_t>(argc)} | std::ranges::views::drop(1);
    std::ranges::for_each(args, [&](std::string_view arg) {
        if (std::ranges::contains(help_flags, arg)) {
            print_usage(argv[0]);
            std::exit(0);
        }
        if (const auto it = std::ranges::find(bool_flags, arg, &BoolFlag::name); it != std::ranges::end(bool_flags)) {
            it->target = true;
        } else if (arg.starts_with('-')) {
            fmt::println(stderr, "Unknown option: {}", arg);
            print_usage(argv[0]);
            std::exit(1);
        } else {
            mountpoint = std::string{arg};
        }
    });

    if (mountpoint.empty()) {
        fmt::println(stderr, "Error: mountpoint required\n");
        print_usage(argv[0]);
        return 1;
    }

    auto logger = spdlog::create_async<spdlog::sinks::stdout_color_sink_mt>("cachyos_logger");
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%r][%^---%L---%$] %v");
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_every(std::chrono::seconds(5));

#ifndef COS_BUILD_STATIC
    // Set gucc logger.
    gucc::logger::set_logger(logger);
#endif

    if (!show_info && !gucc::fs::is_btrfs(mountpoint)) {
        fmt::println(stderr, "{} is not a btrfs filesystem", mountpoint);
        return 1;
    }

    const std::string_view filter = show_all ? ""sv : "@"sv;

    using action_fn                            = void (*)(std::string_view, std::string_view);
    const std::pair<bool, action_fn> actions[] = {
        {show_info, [](auto mp, auto) { print_filesystem_info(mp); }},
        {show_snapshots, [](auto mp, auto f) { print_snapshots(mp, f); }},
        {show_detailed, [](auto mp, auto f) { print_detailed_subvolumes(mp, f); }},
    };

    if (const auto it = std::ranges::find_if(actions, [](const auto& p) { return p.first; }); it != std::ranges::end(actions)) {
        it->second(mountpoint, filter);
    } else {
        print_subvolumes(mountpoint, filter);
    }

    return 0;
}
