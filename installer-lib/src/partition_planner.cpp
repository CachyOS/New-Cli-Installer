#include "cachyos/partition_planner.hpp"
#include "cachyos/disk.hpp"
#include "detail/result.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/btrfs_query.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/luks.hpp"
#include "gucc/lvm.hpp"
#include "gucc/mount_partitions.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/partitioning.hpp"
#include "gucc/ranges_utils.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/system_query.hpp"
#include "gucc/zfs_query.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <random>
#include <ranges>
#include <string_view>
#include <system_error>
#include <utility>

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

// NOLINTNEXTLINE
using namespace cachyos::installer::partition_planner;

auto block_devices_inventory() noexcept -> std::vector<DeviceEntry> {
    auto devs = gucc::disk::list_block_devices();
    if (!devs) {
        return {};
    }

    return std::move(*devs)
        | std::ranges::views::as_rvalue
        | std::ranges::views::transform([](gucc::disk::BlockDevice dev) {
              // TODO(vnepogodin): can be just std::vector<DeviceEntry>
              return DeviceEntry{
                  .name        = std::move(dev.name),
                  .type        = std::move(dev.type),
                  .fstype      = std::move(dev.fstype),
                  .label       = std::move(dev.label).value_or(""),
                  .model       = std::move(dev.model).value_or(""),
                  .size_bytes  = dev.size.value_or(0),
                  .parent      = std::move(dev.pkname).value_or(""),
                  .mountpoints = std::move(dev.mountpoints),
                  .uuid        = std::move(dev.uuid),
                  .partuuid    = std::move(dev.partuuid).value_or(""),
              };
          })
        | std::ranges::to<std::vector<DeviceEntry>>();
}

auto btrfs_subvolumes_inventory(const std::vector<DeviceEntry>& devices) noexcept
    -> std::vector<ExistingBtrfsSubvolume> {
    std::vector<ExistingBtrfsSubvolume> out;
    for (const auto& dev : devices | std::ranges::views::filter([](const DeviceEntry& entry) { return entry.fstype == "btrfs"sv; })) {
        for (const auto& mountpoint : dev.mountpoints) {
            out.append_range(gucc::fs::list_btrfs_subvolumes(mountpoint, ""sv)
                | std::ranges::views::as_rvalue
                | std::ranges::views::transform([&dev](gucc::fs::BtrfsSubvolume subvol) {
                      return ExistingBtrfsSubvolume{.device = dev.name, .subvolume = std::move(subvol.subvolume)};
                  }));
        }
    }
    return out;
}

auto zfs_pools_inventory() noexcept -> std::vector<ExistingZfsPool> {
    return gucc::fs::list_zfs_pools()
        | std::ranges::views::as_rvalue
        | std::ranges::views::transform([](gucc::fs::ZfsPoolInfo pool) { return ExistingZfsPool{.name = std::move(pool.name)}; })
        | std::ranges::to<std::vector<ExistingZfsPool>>();
}

auto lvm_groups_inventory() noexcept -> std::vector<ExistingLvmGroup> {
    return gucc::lvm::show_volume_groups()
        | std::ranges::views::as_rvalue
        | std::ranges::views::transform([](std::pair<std::string, std::string> group) {
              return ExistingLvmGroup{.name = std::move(group.first), .size = std::move(group.second)};
          })
        | std::ranges::to<std::vector<ExistingLvmGroup>>();
}

[[nodiscard]] auto validate_luks_inputs(std::string_view device,
    std::string_view mapper_name,
    std::string_view passphrase) noexcept
    -> std::expected<void, std::string> {
    if (device.empty()) {
        return std::unexpected(std::string{"device is empty"});
    }
    if (mapper_name.empty()) {
        return std::unexpected(std::string{"mapper_name is empty"});
    }
    if (passphrase.empty()) {
        return std::unexpected(std::string{"passphrase is empty"});
    }
    return {};
}

auto open_luks(LuksVersion version, std::string_view passphrase, std::string_view device, std::string_view mapper_name) noexcept
    -> std::expected<void, std::string> {
    return cachyos::installer::lift(gucc::crypto::luks_open(version, passphrase, device, mapper_name),
        fmt::format(FMT_COMPILE("failed to open LUKS partition {} as {}"), device, mapper_name));
}

// lsblk START unit, whatever the physical sector size
constexpr std::uint64_t kSectorBytes{512};
// MBR/GPT headers and alignment slack at either end of a disk
constexpr std::uint64_t kEdgeReserveBytes{1024ULL * 1024ULL};
// smaller gaps are alignment slack
constexpr std::uint64_t kMinFreeBytes{1024ULL * 1024ULL};

auto to_partition_entry(gucc::disk::BlockDevice dev) -> PartitionEntry {
    const auto start = dev.start.value_or(0) * kSectorBytes;
    const auto size  = dev.size.value_or(0);
    auto part_type   = std::move(dev.parttypename).value_or("");
    return PartitionEntry{
        .number      = gucc::disk::parse_partition_number(dev.name),
        .start_bytes = start,
        .end_bytes   = size > 0 ? start + size - 1 : start,
        .size_bytes  = size,
        .device      = std::move(dev.name),
        .fstype      = std::move(dev.fstype),
        // the flags the Manual editor recognizes
        .flags      = part_type == "EFI System"sv ? "boot, esp" : "",
        .part_type  = std::move(part_type),
        .label      = std::move(dev.label).value_or(""),
        .used_bytes = dev.fsused.value_or(0),
    };
}

auto lift_void(gucc::Result<void> res) -> std::expected<void, std::string> {
    return cachyos::installer::lift(std::move(res));
}

auto apply_partition_op(std::string_view device, const PartitionOp& op, std::string& last_created) -> std::expected<void, std::string> {
    using Kind      = PartitionOp::Kind;
    const auto part = gucc::disk::insert_partition_number(device, op.number);
    switch (op.kind) {
    case Kind::NewTable:
        return lift_void(gucc::disk::create_partition_table(device, op.table_type));
    case Kind::Delete:
        return lift_void(gucc::disk::delete_partition(device, op.number));
    case Kind::Resize:
        return lift_void(gucc::disk::shrink_partition(device, op.number, op.fstype, op.size_bytes, op.end_bytes));
    case Kind::Format:
        if (auto res = lift_void(gucc::fs::format_partition(part, gucc::fs::string_to_filesystem_type(op.fstype))); !res || op.label.empty()) {
            return res;
        }
        return lift_void(gucc::disk::set_filesystem_label(part, op.fstype, op.label));
    case Kind::SetFlag:
        return lift_void(gucc::disk::set_partition_flag(device, op.number, op.flags, op.flag_state));
    case Kind::SetLabel:
        return lift_void(gucc::disk::set_filesystem_label(part, op.fstype, op.label));
    case Kind::Create:
        break;
    }

    auto created = cachyos::installer::lift(gucc::disk::create_partition(device, op.start_bytes, op.end_bytes));
    if (!created) {
        return std::unexpected(std::move(created).error());
    }
    last_created = *std::move(created);
    if (!op.fstype.empty()) {
        if (auto res = lift_void(gucc::fs::format_partition(last_created, gucc::fs::string_to_filesystem_type(op.fstype))); !res) {
            return res;
        }
        if (!op.label.empty()) {
            if (auto res = lift_void(gucc::disk::set_filesystem_label(last_created, op.fstype, op.label)); !res) {
                return res;
            }
        }
    }
    const auto number = gucc::disk::parse_partition_number(last_created);
    for (const auto flag : gucc::utils::make_split_view(op.flags, ',')) {
        const auto name = gucc::utils::trim(flag);
        if (name.empty()) {
            continue;
        }
        if (auto res = lift_void(gucc::disk::set_partition_flag(device, number, name, true)); !res) {
            return res;
        }
    }
    return {};
}

}  // namespace

namespace cachyos::installer::partition_planner {

auto discover() noexcept -> DiskInventory {
    DiskInventory inv{};
    inv.block_devices    = block_devices_inventory();
    inv.btrfs_subvolumes = btrfs_subvolumes_inventory(inv.block_devices);
    inv.zfs_pools        = zfs_pools_inventory();
    inv.lvm_groups       = lvm_groups_inventory();
    return inv;
}

auto default_btrfs_layout() noexcept -> std::vector<BtrfsSubvolumeChoice> {
    return convert_subvolumes<BtrfsSubvolumeChoice>(cachyos::installer::default_btrfs_subvolumes());
}

auto suggest_mountpoint_for_subvolume(std::string_view subvol_path) noexcept -> std::string {
    const auto name = gucc::utils::strip_prefix(subvol_path, "/"sv).value_or(subvol_path);
    if (name.empty()) {
        return {};
    }

    static constexpr std::array<std::pair<std::string_view, std::string_view>, 8> kKnown{{
        {"@"sv, "/"sv},
        {"@home"sv, "/home"sv},
        {"@root"sv, "/root"sv},
        {"@srv"sv, "/srv"sv},
        {"@cache"sv, "/var/cache"sv},
        {"@tmp"sv, "/var/tmp"sv},
        {"@log"sv, "/var/log"sv},
        {"@snapshots"sv, "/.snapshots"sv},
    }};
    for (const auto& [subvolname, subvolpath] : kKnown) {
        if (name == subvolname) {
            return std::string{subvolpath};
        }
    }

    // Unknown subvolume. /@
    const auto stem = gucc::utils::strip_prefix(name, "@"sv).value_or(name);
    std::string out{"/"};
    out.append(stem);
    return out;
}

auto inspect_existing_btrfs(std::string_view device) noexcept
    -> std::expected<std::vector<std::string>, std::string> {
    if (device.empty()) {
        return std::unexpected(std::string{"device is empty"});
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    const auto scratch = fs::temp_directory_path() / fmt::format("cachyos-btrfs-inspect-{}", std::random_device{}());

    fs::create_directories(scratch, ec);
    if (ec) {
        return std::unexpected(fmt::format("failed to create scratch dir: {}", ec.message()));
    }

    std::vector<gucc::fs::BtrfsSubvolume> subvols;
    {
        // discovery only, never modify the source FS
        auto mount = gucc::mount::ScopedMount::mount(device, scratch.string(), "ro"sv, "btrfs"sv);
        if (!mount) {
            fs::remove_all(scratch, ec);
            return std::unexpected(fmt::format(FMT_COMPILE("failed to mount {} for inspection: {}"), device, mount.error().context));
        }
        subvols = gucc::fs::list_btrfs_subvolumes(scratch.string(), ""sv);
    }
    fs::remove_all(scratch, ec);

    return std::move(subvols)
        | std::ranges::views::as_rvalue
        | std::ranges::views::transform(&gucc::fs::BtrfsSubvolume::subvolume)
        | std::ranges::to<std::vector<std::string>>();
}

auto default_zfs_layout(std::string_view zpool_name) noexcept
    -> std::vector<ZfsDatasetChoice> {
    return cachyos::installer::default_zfs_datasets(zpool_name)
        | std::ranges::views::as_rvalue
        | std::ranges::views::transform([](gucc::fs::ZfsDataset dataset) {
              return ZfsDatasetChoice{.dataset = std::move(dataset.zpath), .mountpoint = std::move(dataset.mountpoint)};
          })
        | std::ranges::to<std::vector<ZfsDatasetChoice>>();
}

auto prepare_default_zpool(std::string_view partition,
    std::string_view zpool_name,
    std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    return cachyos::installer::zfs_auto_pres(partition, zpool_name, mountpoint).transform([](auto&&) { });
}

auto encrypt_partition(const LuksFormatRequest& req) noexcept
    -> std::expected<void, std::string> {
    if (auto v = validate_luks_inputs(req.device, req.mapper_name, req.passphrase); !v) {
        return v;
    }
    if (auto res = lift(gucc::crypto::luks_format(req.version, req.passphrase, req.device, req.extra_flags),
            fmt::format(FMT_COMPILE("failed to format LUKS partition {}"), req.device));
        !res) {
        return res;
    }
    return open_luks(req.version, req.passphrase, req.device, req.mapper_name);
}

auto open_encrypted_partition(const LuksOpenRequest& req) noexcept
    -> std::expected<void, std::string> {
    if (auto v = validate_luks_inputs(req.device, req.mapper_name, req.passphrase); !v) {
        return v;
    }
    return open_luks(req.version, req.passphrase, req.device, req.mapper_name);
}

auto finalize_plan(PartitionPlan plan) noexcept
    -> std::expected<MountSelections, std::string> {
    if (plan.root.device.empty()) {
        return std::unexpected(std::string{"root device is empty"});
    }
    if (plan.root.fstype.empty()) {
        return std::unexpected(std::string{"root fstype is empty"});
    }
    if (plan.swap.type == SwapSelection::Type::Partition && plan.swap.device.empty()) {
        return std::unexpected(std::string{"swap partition selected, but device is empty"});
    }

    MountSelections out;
    out.root       = std::move(plan.root);
    out.swap       = std::move(plan.swap);
    out.esp        = std::move(plan.esp);
    out.additional = std::move(plan.additional);

    out.btrfs_subvolumes = convert_subvolumes<gucc::fs::BtrfsSubvolume>(std::move(plan.btrfs_subvolumes));
    return out;
}

auto list_partitions(std::string_view device) noexcept -> std::vector<PartitionEntry> {
    auto devices = gucc::disk::list_block_devices();
    if (!devices) {
        return {};
    }
    auto partitions = *std::move(devices)
        | std::ranges::views::as_rvalue
        | std::ranges::views::filter([device](const gucc::disk::BlockDevice& dev) { return dev.type == "part"sv && dev.pkname == device; })
        | std::ranges::views::transform(to_partition_entry)
        | std::ranges::to<std::vector<PartitionEntry>>();
    std::ranges::sort(partitions, {}, &PartitionEntry::start_bytes);
    return partitions;
}

auto list_free_space(std::string_view device) noexcept -> std::vector<FreeSpaceRegion> {
    const auto total = gucc::disk::query_block_device(device)
                           .and_then([](const gucc::disk::BlockDevice& dev) { return dev.size; })
                           .value_or(0);
    if (total <= 2 * kEdgeReserveBytes) {
        return {};
    }

    std::vector<FreeSpaceRegion> gaps;
    const auto add_gap = [&gaps](std::uint64_t start, std::uint64_t end_exclusive) {
        if (end_exclusive > start && end_exclusive - start >= kMinFreeBytes) {
            gaps.push_back({.start_bytes = start, .end_bytes = end_exclusive - 1, .size_bytes = end_exclusive - start});
        }
    };
    auto cursor = kEdgeReserveBytes;
    for (const auto& part : list_partitions(device)) {
        if (part.size_bytes == 0) {
            continue;
        }
        add_gap(cursor, part.start_bytes);
        cursor = std::max(cursor, part.end_bytes + 1);
    }
    add_gap(cursor, total - kEdgeReserveBytes);
    return gaps;
}

auto shrinkable_bounds(std::string_view device, std::uint32_t number,
    std::string_view fstype, std::uint64_t current_size_bytes) noexcept
    -> std::expected<std::pair<std::uint64_t, std::uint64_t>, std::string> {
    // lsblk knows the usage of mounted filesystems only
    const auto partitions = list_partitions(device);
    const auto* entry     = gucc::utils::find_ptr(partitions, number, &PartitionEntry::number);
    if (entry != nullptr && entry->used_bytes > 0) {
        return std::pair{entry->used_bytes, current_size_bytes};
    }
    return lift(gucc::disk::filesystem_used_bytes(gucc::disk::insert_partition_number(device, number), fstype))
        .transform([current_size_bytes](std::uint64_t used) { return std::pair{used, current_size_bytes}; });
}

auto apply_partition_plan(std::string_view device, const std::vector<PartitionOp>& ops,
    const std::vector<StagedMount>& mounts) noexcept
    -> std::expected<MountSelections, std::string> {
    std::string created;
    for (const auto& op : ops) {
        if (auto res = apply_partition_op(device, op, created); !res) {
            return std::unexpected(std::move(res).error());
        }
    }

    PartitionPlan plan{};
    bool has_root{};
    for (const auto& mount : mounts) {
        auto path = mount.created ? created : mount.device;
        if (path.empty()) {
            return std::unexpected(fmt::format(FMT_COMPILE("no device for mountpoint '{}'"), mount.mountpoint));
        }
        if (mount.luks) {
            const LuksFormatRequest luks{
                .device      = path,
                .mapper_name = mount.luks_mapper_name,
                .passphrase  = mount.luks_passphrase,
                .extra_flags = {},
                .version     = mount.luks_version,
            };
            if (auto res = encrypt_partition(luks); !res) {
                return std::unexpected(std::move(res).error());
            }
            path = gucc::disk::mapper_path(mount.luks_mapper_name);
        }
        if (mount.mountpoint == "/"sv) {
            plan.root = {.device = std::move(path), .fstype = mount.fstype, .mkfs_command = {}, .mount_opts = mount.mount_opts, .format_requested = mount.format_requested};
            has_root  = true;
        } else if (mount.is_esp) {
            plan.esp = {.device = std::move(path), .mountpoint = mount.mountpoint.empty() ? "/boot/efi" : mount.mountpoint, .format_requested = mount.format_requested};
        } else {
            plan.additional.push_back({.device = std::move(path), .mountpoint = mount.mountpoint, .fstype = mount.fstype, .mkfs_command = {}, .mount_opts = mount.mount_opts, .format_requested = mount.format_requested});
        }
    }
    if (!has_root) {
        return std::unexpected(std::string{"the partition plan has no '/' mount"});
    }
    if (plan.root.fstype == "btrfs"sv) {
        plan.btrfs_subvolumes = default_btrfs_layout();
    }
    return finalize_plan(std::move(plan));
}

}  // namespace cachyos::installer::partition_planner
