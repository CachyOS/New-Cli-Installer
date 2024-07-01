#include "gucc/crypttab.hpp"
#include "gucc/partition.hpp"

#include <algorithm>  // for any_of, sort, unique_copy

#include <filesystem>
#include <fstream>

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/algorithm/sort.hpp>
#include <range/v3/algorithm/unique_copy.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

// NOLINTNEXTLINE
static constexpr auto CRYPTTAB_HEADER = R"(# Configuration for encrypted block devices
# See crypttab(5) for details.

# NOTE: Do not list your root (/) partition here, it must be set up
#       beforehand by the initramfs (/etc/mkinitcpio.conf).

# <name>       <device>                                     <password>              <options>
)"sv;

}  // namespace

namespace gucc::fs {

auto gen_crypttab_entry(const Partition& partition, std::string_view crypttab_opts, bool is_root_encrypted, bool is_boot_encrypted) noexcept -> std::optional<std::string> {
    // skip if partition is not encypted
    if (!partition.luks_mapper_name || !partition.luks_uuid) {
        return std::nullopt;
    }
    // skip invalid usage
    if (partition.luks_mapper_name->empty() || partition.luks_uuid->empty()) {
        return std::nullopt;
    }

    auto crypt_password = "/crypto_keyfile.bin"s;
    auto crypt_options  = fmt::format(FMT_COMPILE(" {}"), crypttab_opts);

    // 1. if root partition is not encrypted
    // 2. if root partition is encrypted, but boot partition is not encrypted
    if (!is_root_encrypted || (partition.mountpoint == "/"sv && !is_boot_encrypted)) {
        crypt_password = "none"s;
        crypt_options  = ""s;
    }

    const auto& device_str = fmt::format(FMT_COMPILE("UUID={}"), *partition.luks_uuid);
    return std::make_optional<std::string>(fmt::format(FMT_COMPILE("{:21} {:<45} {}{}\n"), *partition.luks_mapper_name, device_str, crypt_password, crypt_options));
}

auto generate_crypttab_content(const std::vector<Partition>& partitions, std::string_view crypttab_opts) noexcept -> std::string {
    std::string crypttab_content{CRYPTTAB_HEADER};

    // sort by mountpoint & device
    auto partitions_sorted{partitions};
    ranges::sort(partitions_sorted, {}, &Partition::mountpoint);
    ranges::sort(partitions_sorted, {}, &Partition::device);

    // filter duplicates
    std::vector<Partition> partitions_filtered{};
    ranges::unique_copy(
        partitions_sorted, std::back_inserter(partitions_filtered),
        {},
        &Partition::device);

    const bool is_root_encrypted = ranges::any_of(partitions, [](auto&& part) { return part.mountpoint == "/"sv && part.luks_mapper_name; });
    const bool is_boot_encrypted = ranges::any_of(partitions, [](auto&& part) { return part.mountpoint == "/bool"sv && part.luks_mapper_name; });

    for (auto&& partition : partitions_filtered) {
        auto crypttab_entry = gen_crypttab_entry(partition, crypttab_opts, is_root_encrypted, is_boot_encrypted);
        if (!crypttab_entry) {
            continue;
        }
        crypttab_content += *crypttab_entry;
    }

    return crypttab_content;
}

auto generate_crypttab(const std::vector<Partition>& partitions, std::string_view root_mountpoint, std::string_view crypttab_opts) noexcept -> bool {
    const auto& crypttab_filepath = fmt::format(FMT_COMPILE("{}/etc/crypttab"), root_mountpoint);

    std::ofstream crypttab_file{crypttab_filepath, std::ios::out | std::ios::trunc};
    if (!crypttab_file.is_open()) {
        spdlog::error("Failed to open crypttab for writing {}", crypttab_filepath);
        return false;
    }
    crypttab_file << fs::generate_crypttab_content(partitions, crypttab_opts);
    return true;
}

}  // namespace gucc::fs
