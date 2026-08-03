#include "cachyos/headless_plan.hpp"

#include "cachyos/partition_planner.hpp"

// import gucc
#include "gucc/partition_config.hpp"
#include "gucc/partitioning.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/system_query.hpp"

#include <algorithm>    // for sort, count_if, find
#include <cstdint>      // for uint32_t
#include <iterator>     // for back_inserter
#include <optional>     // for optional
#include <ranges>       // for ranges::*
#include <string_view>  // for string_view
#include <utility>      // for move

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

using cachyos::installer::PartitionConfig;
using cachyos::installer::PartitionType;

struct NumberedPartition final {
    std::uint32_t number{};
    const PartitionConfig* config{};
};

// pair each declared partition with its number
[[nodiscard]] auto number_partitions(const std::vector<PartitionConfig>& partitions,
    std::string_view device, std::vector<std::string>& errors) noexcept -> std::vector<NumberedPartition> {
    std::vector<NumberedPartition> numbered{};
    numbered.reserve(partitions.size());

    for (const auto& part : partitions) {
        const auto number = gucc::disk::parse_partition_number(part.name);
        if (number == 0) {
            errors.push_back(fmt::format(FMT_COMPILE("partition '{}': name does not end in a partition number"), part.name));
            continue;
        }
        const auto& expected = gucc::disk::insert_partition_number(device, number);
        if (expected != part.name) {
            errors.push_back(fmt::format(FMT_COMPILE("partition '{}' is not partition {} of device '{}' (that would be '{}')"),
                part.name, number, device, expected));
            continue;
        }
        numbered.push_back(NumberedPartition{.number = number, .config = &part});
    }

    return numbered;
}

void validate_contiguous_numbering(const std::vector<NumberedPartition>& sorted,
    std::vector<std::string>& errors) noexcept {
    for (const auto& [index, entry] : sorted | std::ranges::views::enumerate) {
        const auto expected = static_cast<std::uint32_t>(index) + 1U;
        if (entry.number == expected) {
            continue;
        }
        if (index > 0 && gucc::utils::index_viewable_range(sorted, index - 1)->number == entry.number) {
            errors.push_back(fmt::format(FMT_COMPILE("partition '{}': number {} is declared twice"),
                entry.config->name, entry.number));
        } else {
            errors.push_back(fmt::format(FMT_COMPILE("partition '{}': partition numbers must run 1..{} without gaps, expected number {} here"),
                entry.config->name, sorted.size(), expected));
        }
    }
}

// NOTE: fs_name is already resolved by the parser, root inherits the global one
// there and every other type has to carry its own
[[nodiscard]] auto to_gucc_partition(const PartitionConfig& part,
    const std::optional<std::string>& mount_opts, bool is_ssd) noexcept -> gucc::fs::Partition {
    const auto fs_type = gucc::fs::string_to_filesystem_type(part.fs_name);
    return gucc::fs::Partition{
        .fstype     = part.fs_name,
        .mountpoint = part.mountpoint,
        .uuid_str   = {},
        .device     = part.name,
        .size       = part.size,
        .mount_opts = mount_opts.value_or(gucc::fs::get_default_mount_opts(fs_type, is_ssd)),
    };
}

// checks about the partition set as a whole, not a single entry
void validate_layout(const std::vector<NumberedPartition>& sorted, bool is_efi,
    std::vector<std::string>& errors) noexcept {
    const auto count_of_type = [&sorted](PartitionType type) {
        auto functor = [](const NumberedPartition& entry) { return entry.config->type; };
        return std::ranges::count(sorted, type, std::move(functor));
    };

    if (const auto roots = count_of_type(PartitionType::Root); roots != 1) {
        errors.push_back(fmt::format(FMT_COMPILE("expected exactly one partition of type 'root', found {}"), roots));
    }

    const auto boots = count_of_type(PartitionType::Boot);
    if (is_efi && boots != 1) {
        errors.push_back(fmt::format(FMT_COMPILE("a UEFI install needs exactly one partition of type 'boot', found {}"), boots));
    } else if (boots > 1) {
        errors.push_back(fmt::format(FMT_COMPILE("expected at most one partition of type 'boot', found {}"), boots));
    }

    for (const auto& [index, entry] : sorted | std::ranges::views::enumerate) {
        const auto& part = *entry.config;
        if (gucc::fs::string_to_filesystem_type(part.fs_name) == gucc::fs::FilesystemType::Unknown) {
            errors.push_back(fmt::format(FMT_COMPILE("partition '{}': unknown filesystem '{}'"), part.name, part.fs_name));
        }

        // reject percentages
        if (const auto size = gucc::utils::trim(part.size); gucc::utils::contains(size, "%"sv) && size != "100%"sv) {
            errors.push_back(fmt::format(FMT_COMPILE("partition '{}': size '{}' is not supported; use a byte size (e.g. '512M', '100G') or '100\%' for the rest of the disk"), part.name, part.size));
        }

        // a duplicate mountpoint means one gets shadowed by the other at mount
        // time. only report the later of each pair
        if (part.mountpoint.empty()) {
            continue;
        }
        const auto preceding_end = gucc::utils::index_viewable_range(sorted, index);
        const auto earlier       = std::ranges::find(std::ranges::begin(sorted), preceding_end, part.mountpoint,
            [](const NumberedPartition& entry) { return entry.config->mountpoint; });
        if (earlier != preceding_end) {
            errors.push_back(fmt::format(FMT_COMPILE("partitions '{}' and '{}' both claim mountpoint '{}'"),
                earlier->config->name, part.name, part.mountpoint));
        }
    }
}

template <typename T>
concept BtrfsSubvolHolder = requires(const T& v) {
    gucc::fs::BtrfsSubvolume{.subvolume = v.subvolume, .mountpoint = v.mountpoint};
};

// convert any range of subvolume-like into gucc BtrfsSubvolumes.
template <std::ranges::input_range R>
    requires BtrfsSubvolHolder<std::ranges::range_value_t<R>>
[[nodiscard]] auto conv_to_btrfs_subvols(R&& subvols) noexcept
    -> std::vector<gucc::fs::BtrfsSubvolume> {
    return std::forward<R>(subvols)
        | std::ranges::views::transform([](const BtrfsSubvolHolder auto& subvol) {
              return gucc::fs::BtrfsSubvolume{.subvolume = subvol.subvolume, .mountpoint = subvol.mountpoint};
          })
        | std::ranges::to<std::vector<gucc::fs::BtrfsSubvolume>>();
}

}  // namespace

namespace cachyos::installer {

auto headless_strategy_from_config(const InstallerConfig& cfg, bool is_efi) noexcept
    -> std::expected<PartitionStrategy, std::vector<std::string>> {
    std::vector<std::string> errors{};

    const auto& device = cfg.device.value_or(""s);
    if (device.empty()) {
        errors.emplace_back("'device' is required: there is no target disk to prepare");
    }

    if (cfg.partitions.empty()) {
        if (!cfg.allow_auto_partition) {
            errors.push_back(fmt::format(FMT_COMPILE("'partitions' is empty and 'allow_auto_partition' is not set; refusing to erase '{}' and guess a layout"), device));
            return std::unexpected(std::move(errors));
        }
        if (!errors.empty()) {
            return std::unexpected(std::move(errors));
        }
        return PartitionStrategy{partition_strategy::EraseAndAuto{.device = device}};
    }

    auto numbered = number_partitions(cfg.partitions, device, errors);
    std::ranges::sort(numbered, {}, &NumberedPartition::number);
    validate_contiguous_numbering(numbered, errors);
    validate_layout(numbered, is_efi, errors);

    const auto is_ssd    = gucc::disk::is_device_ssd(device);
    auto converted_parts = numbered
        | std::ranges::views::transform([&cfg, is_ssd](const NumberedPartition& entry) {
              return to_gucc_partition(*entry.config, cfg.mount_opts, is_ssd);
          })
        | std::ranges::to<std::vector<gucc::fs::Partition>>();

    // NOTE(vnepogodin): subvolumes on a btrfs root accepted atm
    // what do we do for zfs??? it must be also supported
    const auto root_entry    = std::ranges::find(numbered, PartitionType::Root,
        [](const NumberedPartition& entry) { return entry.config->type; });
    const auto root_fs_name  = (root_entry != std::ranges::end(numbered)) ? std::string_view{root_entry->config->fs_name} : ""sv;
    const bool root_is_btrfs = root_fs_name == "btrfs"sv;

    std::vector<gucc::fs::BtrfsSubvolume> btrfs_subvolumes{};
    if (!cfg.subvolumes.empty()) {
        if (!root_is_btrfs) {
            errors.push_back(fmt::format(FMT_COMPILE("'subvolumes' requires a btrfs root filesystem, but root is '{}'"), root_fs_name));
        } else {
            btrfs_subvolumes = conv_to_btrfs_subvols(cfg.subvolumes);
        }
    } else if (root_is_btrfs && cfg.use_default_subvolumes) {
        btrfs_subvolumes = conv_to_btrfs_subvols(partition_planner::default_btrfs_layout());
    }

    // only errors block the install
    auto schema_validation = gucc::disk::validate_partition_schema(converted_parts, device, is_efi);
    std::ranges::move(schema_validation.errors, std::back_inserter(errors));

    if (!errors.empty()) {
        return std::unexpected(std::move(errors));
    }

    return PartitionStrategy{partition_strategy::CreateLayout{
        .device           = device,
        .partitions       = std::move(converted_parts),
        .btrfs_subvolumes = std::move(btrfs_subvolumes),
    }};
}

}  // namespace cachyos::installer
