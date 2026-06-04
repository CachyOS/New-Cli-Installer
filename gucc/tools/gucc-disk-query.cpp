#include "gucc/system_query.hpp"
#ifndef COS_BUILD_STATIC
#include "gucc/logger.hpp"
#endif

#include <cstdlib>
#include <string>
#include <string_view>

#include <fmt/color.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <spdlog/async.h>                     // for create_async
#include <spdlog/common.h>                    // for debug
#include <spdlog/sinks/stdout_color_sinks.h>  // for stdout_color_sink_mt
#include <spdlog/spdlog.h>                    // for set_default_logger, set_level

using namespace std::string_view_literals;

void print_usage(const char* program_name) {
    fmt::println(stderr, "Usage: {} [OPTIONS] [DEVICE]", program_name);
    fmt::println(stderr, "\nQuery disk and partition information.");
    fmt::println(stderr, "\nOptions:");
    fmt::println(stderr, "  -h, --help     Show this help message");
    fmt::println(stderr, "  -s, --schema   Output partition schema for device");
    fmt::println(stderr, "\nExamples:");
    fmt::println(stderr, "  {}              List all disks", program_name);
    fmt::println(stderr, "  {} /dev/sda     Show info for specific disk", program_name);
    fmt::println(stderr, "  {} -s /dev/sda  Show partition schema for disk", program_name);
}

void print_disk_info(const gucc::disk::DiskInfo& disk) {
    // Print disk header
    fmt::print(fmt::emphasis::bold, "{}", disk.device);
    if (disk.model) {
        fmt::print(" - {}", *disk.model);
    }
    fmt::println("");

    // Print disk details
    fmt::println("  Size:      {}", gucc::disk::format_size(disk.size));
    fmt::println("  Transport: {}", gucc::disk::disk_transport_to_string(disk.transport));
    fmt::println("  SSD:       {}", disk.is_ssd ? "yes" : "no");
    fmt::println("  Removable: {}", disk.is_removable ? "yes" : "no");
    if (disk.pttype) {
        fmt::println("  Label:     {}", *disk.pttype);
    }

    // Print partitions
    if (!disk.partitions.empty()) {
        fmt::println("\n  Partitions:");
        fmt::println("  {:20} {:12} {:10} {:12} {}", "Device", "Size", "Type", "Label", "Mountpoint");
        fmt::println("  {:->68}", "");

        for (const auto& part : disk.partitions) {
            std::string label_str  = part.label.value_or("-");
            std::string mount_str  = part.mountpoints.empty() ? "-" : fmt::format("{}", fmt::join(part.mountpoints, ", "));
            std::string fstype_str = part.fstype.empty() ? "-" : part.fstype;

            fmt::println("  {:20} {:12} {:10} {:12} {}",
                part.device,
                gucc::disk::format_size(part.size),
                fstype_str,
                label_str,
                mount_str);
        }
    }
    fmt::println("");
}

void print_partition_schema(std::string_view device) {
    auto schema = gucc::disk::get_partition_schema(device);
    if (schema.empty()) {
        fmt::println(stderr, "No partitions found on {}", device);
        return;
    }

    fmt::println("Partition schema for {}:\n", device);
    fmt::println("{:20} {:12} {:10} {:15} {}", "Device", "Size", "Filesystem", "Mountpoint", "Options");
    fmt::println("{:->80}", "");

    for (const auto& part : schema) {
        std::string size_str  = part.size.empty() ? "<remaining>" : part.size;
        std::string mount_str = part.mountpoint.empty() ? "-" : part.mountpoint;
        std::string opts_str  = part.mount_opts.empty() ? "defaults" : part.mount_opts;

        // Truncate long options
        if (opts_str.size() > 25) {
            opts_str = opts_str.substr(0, 22) + "...";
        }

        fmt::println("{:20} {:12} {:10} {:15} {}",
            part.device,
            size_str,
            part.fstype,
            mount_str,
            opts_str);
    }
}

void list_all_disks() {
    auto disks = gucc::disk::list_disks();
    if (!disks || disks->empty()) {
        fmt::println(stderr, "No disks found");
        return;
    }

    fmt::print(fmt::emphasis::bold, "Found {} disk(s):\n\n", disks->size());

    for (const auto& disk : *disks) {
        print_disk_info(disk);
    }
}

void show_disk_info(std::string_view device) {
    auto disk = gucc::disk::get_disk_info(device);
    if (!disk) {
        fmt::println(stderr, "Failed to get info for device: {}", device);
        return;
    }

    print_disk_info(*disk);
}

int main(int argc, char** argv) {
    bool show_schema = false;
    std::string device;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "-h"sv || arg == "--help"sv) {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-s"sv || arg == "--schema"sv) {
            show_schema = true;
        } else if (arg.starts_with('-')) {
            fmt::println(stderr, "Unknown option: {}", arg);
            print_usage(argv[0]);
            return 1;
        } else {
            device = std::string{arg};
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

    // Execute requested action
    if (device.empty()) {
        list_all_disks();
    } else if (show_schema) {
        print_partition_schema(device);
    } else {
        show_disk_info(device);
    }

    return 0;
}
