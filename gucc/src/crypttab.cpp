#include "gucc/crypttab.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/partition.hpp"

#include <algorithm>  // for any_of, sort, unique_copy
#include <ranges>     // for ranges::*

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

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
    std::ranges::sort(partitions_sorted, {}, &Partition::mountpoint);
    std::ranges::sort(partitions_sorted, {}, &Partition::device);

    // filter duplicates
    std::vector<Partition> partitions_filtered{};
    std::ranges::unique_copy(
        partitions_sorted, std::back_inserter(partitions_filtered),
        {},
        &Partition::device);

    const bool is_root_encrypted = std::ranges::any_of(partitions, [](auto&& part) { return part.mountpoint == "/"sv && part.luks_mapper_name; });
    const bool is_boot_encrypted = std::ranges::any_of(partitions, [](auto&& part) { return part.mountpoint == "/bool"sv && part.luks_mapper_name; });

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
    const auto& crypttab_content  = fs::generate_crypttab_content(partitions, crypttab_opts);
    if (!file_utils::create_file_for_overwrite(crypttab_filepath, crypttab_content)) {
        spdlog::error("Failed to open crypttab for writing {}", crypttab_filepath);
        return false;
    }
    return true;
}

}  // namespace gucc::fs
