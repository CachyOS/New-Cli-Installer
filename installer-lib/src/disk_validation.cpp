#include "cachyos/disk_validation.hpp"

#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/system_query.hpp"

#include <algorithm>  // for max, min
#include <charconv>   // for from_chars
#include <expected>   // for expected
#include <filesystem> // for exists, ifstream
#include <fstream>    // for ifstream
#include <ranges>     // for ranges::*
#include <string_view>// for string_view

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace {

constexpr auto to_lower(std::string_view s) noexcept -> std::string {
    std::string result{};
    result.reserve(s.size());
    std::ranges::transform(s, std::back_inserter(result), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

constexpr auto is_efi_fs(std::string_view fs) noexcept -> bool {
    const auto f = to_lower(fs);
    return f == "vfat" || f == "fat32" || f == "fat16";
}

constexpr auto is_swap_fs(std::string_view fs) noexcept -> bool {
    const auto f = to_lower(fs);
    return f == "linuxswap" || f == "swap";
}

// Suffix multipliers for size strings (longest suffix first).
constexpr std::array kSuffixes{
    std::pair{"YiB"sv, uint64_t{1} << 60},
    std::pair{"PiB"sv, uint64_t{1} << 50},
    std::pair{"TiB"sv, uint64_t{1} << 40},
    std::pair{"GiB"sv, uint64_t{1} << 30},
    std::pair{"MiB"sv, uint64_t{1} << 20},
    std::pair{"KiB"sv, uint64_t{1} << 10},
    std::pair{"TB"sv,  uint64_t{1000} * 1000 * 1000 * 1000},
    std::pair{"GB"sv,  uint64_t{1000} * 1000 * 1000},
    std::pair{"MB"sv,  uint64_t{1000} * 1000},
    std::pair{"KB"sv,  uint64_t{1000}},
    std::pair{"P"sv,   uint64_t{1} << 50},
    std::pair{"T"sv,   uint64_t{1} << 40},
    std::pair{"G"sv,   uint64_t{1} << 30},
    std::pair{"M"sv,   uint64_t{1} << 20},
    std::pair{"K"sv,   uint64_t{1} << 10},
};

constexpr uint64_t kRecommendedEfiSize = 512ULL * 1024 * 1024;
constexpr uint64_t kMinimumEfiSize     = 100ULL * 1024 * 1024;

auto get_total_ram_bytes() noexcept -> uint64_t {
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) return 0;

    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.starts_with("MemTotal:"s)) {
            auto pos = line.find(':');
            if (pos == std::string::npos) return 0;
            auto num_str = line.substr(pos + 1);
            uint64_t kb{};
            if (std::from_chars(num_str.data(), num_str.data() + num_str.size(), kb).ec != std::errc{}) {
                if (kb > UINT64_MAX / 1024) return 0;
                return kb * 1024;
            }
            break;
        }
    }
    return 0;
}

auto find_partition_size(const std::vector<cachyos::installer::PartitionConfig>& partitions,
    std::string_view mountpoint) noexcept -> std::optional<uint64_t> {
    for (const auto& part : partitions) {
        if (part.mountpoint == mountpoint) {
            return cachyos::installer::parse_size_bytes(part.size);
        }
    }
    return std::nullopt;
}

auto find_any_partition(const std::vector<cachyos::installer::PartitionConfig>& partitions,
    bool (*predicate)(std::string_view)) noexcept -> std::optional<uint64_t> {
    for (const auto& part : partitions) {
        if (predicate(part.fs_name)) {
            return cachyos::installer::parse_size_bytes(part.size);
        }
    }
    return std::nullopt;
}

}  // namespace

namespace cachyos::installer {

auto parse_size_bytes(std::string_view size_str) noexcept -> std::optional<uint64_t> {
    auto trimmed = gucc::utils::trim(size_str);
    if (trimmed == "100%" || trimmed.empty()) return std::nullopt;

    for (const auto& [suffix, mult] : kSuffixes) {
        if (trimmed.size() >= suffix.size() &&
            trimmed.substr(trimmed.size() - suffix.size()) == suffix) {
            auto num_str = trimmed.substr(0, trimmed.size() - suffix.size());
            uint64_t value{};
            if (std::from_chars(num_str.data(), num_str.data() + num_str.size(), value).ec == std::errc{}) {
                if (value > 0 && mult > UINT64_MAX / value) return std::nullopt;
                return value * mult;
            }
            break;
        }
    }

    uint64_t value{};
    if (std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value).ec == std::errc{}) {
        return value;
    }
    return std::nullopt;
}

auto recommend_swap_size(uint64_t ram_bytes) noexcept -> uint64_t {
    constexpr uint64_t one_gib = uint64_t{1} << 30;

    if (ram_bytes < 2 * one_gib) return 2 * ram_bytes;
    if (ram_bytes < 8 * one_gib) return ram_bytes;
    if (ram_bytes < 64 * one_gib) return std::max(ram_bytes / 2, 4 * one_gib);
    return 4 * one_gib;
}

auto validate_disk_space(const InstallerConfig& config, bool is_efi) noexcept -> std::expected<void, std::string> {
    const auto disk_info = gucc::disk::get_disk_info(config.device.value_or("<not specified>"s));
    if (!disk_info) {
        return std::unexpected(fmt::format("Cannot read disk info for '{}'. Is the device present?", config.device.value_or("<not specified>"s)));
    }

    const uint64_t disk_size = disk_info->size;
    if (disk_size == 0) {
        return std::unexpected(fmt::format("Disk '{}' reports zero size", config.device.value_or("<unknown>"s)));
    }

    // Phase 1: check each explicitly-sized partition fits on disk.
    uint64_t explicit_total{};
    for (const auto& part : config.partitions) {
        const auto size = cachyos::installer::parse_size_bytes(part.size);
        if (!size) continue;

        if (*size > disk_size) {
            return std::unexpected(fmt::format("Partition '{}' requires {} but disk is only {}",
                part.mountpoint, gucc::disk::format_size(*size), gucc::disk::format_size(disk_size)));
        }
        explicit_total += *size;
    }

    // Phase 2: total explicit sizes must not exceed disk.
    if (explicit_total > disk_size) {
        return std::unexpected(fmt::format("Total partition sizes ({}) exceed disk capacity ({})",
            gucc::disk::format_size(explicit_total), gucc::disk::format_size(disk_size)));
    }

    const auto remaining = disk_size - explicit_total;

    // Phase 3: EFI partition size check.
    if (is_efi) {
        const auto efi_size = find_any_partition(config.partitions, is_efi_fs);
        if (!efi_size) {
            return std::unexpected("UEFI system detected but no EFI (vfat) partition found");
        }

        if (*efi_size < kMinimumEfiSize) {
            return std::unexpected(fmt::format("EFI partition is {} — minimum required is {}",
                gucc::disk::format_size(*efi_size), gucc::disk::format_size(kMinimumEfiSize)));
        }
        if (*efi_size < kRecommendedEfiSize) {
            spdlog::warn("EFI partition is {} — meets minimum but {} recommended",
                gucc::disk::format_size(*efi_size), gucc::disk::format_size(kRecommendedEfiSize));
        }
    }

    // Phase 4: swap size check.
    const uint64_t ram_bytes = get_total_ram_bytes();
    if (ram_bytes > 0) {
        const auto recommended_swap = recommend_swap_size(ram_bytes);
        const auto swap_size = find_any_partition(config.partitions, is_swap_fs);

        if (swap_size) {
            if (*swap_size < recommended_swap / 2) {
                spdlog::warn("Swap ({}) is smaller than recommended ({}) for {} RAM",
                    gucc::disk::format_size(*swap_size),
                    gucc::disk::format_size(recommended_swap),
                    gucc::disk::format_size(ram_bytes));
            }
        } else if (config.encrypt_swap && config.device.has_value()) {
            spdlog::info("encrypt_swap enabled — swap will be created on target device");
        } else if (!config.allow_auto_partition && !config.partitions.empty() && ram_bytes > 4ULL * (1ULL << 30)) {
            spdlog::warn("No swap partition defined. {} RAM detected — consider adding one",
                gucc::disk::format_size(ram_bytes));
        }
    } else {
        spdlog::info("Could not read /proc/meminfo — skipping swap validation");
    }

    // Phase 5: remaining space sanity check.
    if (remaining < 1ULL << 30) {
        spdlog::warn("Only {} remaining after explicit partitions", gucc::disk::format_size(remaining));
    }

    return {};
}

}  // namespace cachyos::installer
