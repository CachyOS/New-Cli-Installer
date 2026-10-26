#include "cachyos/partition_planner.hpp"
#include "cachyos/disk.hpp"
#include "cachyos/result.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/btrfs_query.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/luks.hpp"
#include "gucc/lvm.hpp"
#include "gucc/mount_partitions.hpp"
#include "gucc/string_utils.hpp"
#include "gucc/zfs_query.hpp"

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

}  // namespace cachyos::installer::partition_planner
