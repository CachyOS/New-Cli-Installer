#include "cachyos/partition_planner.hpp"
#include "cachyos/disk.hpp"

// import gucc
#include "gucc/block_devices.hpp"
#include "gucc/btrfs_query.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/luks.hpp"
#include "gucc/lvm.hpp"
#include "gucc/partitioning.hpp"
#include "gucc/system_query.hpp"
#include "gucc/zfs_query.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <random>
#include <sstream>
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

auto to_device_entry(gucc::disk::BlockDevice&& d) noexcept -> DeviceEntry {
    return DeviceEntry{
        .name        = std::move(d.name),
        .type        = std::move(d.type),
        .fstype      = std::move(d.fstype),
        .label       = d.label.value_or(""),
        .model       = d.model.value_or(""),
        .size_bytes  = d.size.value_or(0),
        .parent      = d.pkname.value_or(""),
        .mountpoints = std::move(d.mountpoints),
        .uuid        = std::move(d.uuid),
        .partuuid    = d.partuuid.value_or(""),
    };
}

auto from_disk_info(gucc::disk::DiskInfo&& d) noexcept -> DeviceEntry {
    return DeviceEntry{
        .name        = std::move(d.device),
        .type        = std::string{"disk"},
        .fstype      = {},
        .label       = {},
        .model       = d.model.value_or(""),
        .size_bytes  = d.size,
        .parent      = {},
        .mountpoints = {},
        .uuid        = {},
        .partuuid    = {},
    };
}

auto block_devices_inventory() noexcept -> std::vector<DeviceEntry> {
    std::vector<DeviceEntry> out;

    // Top-level disks first so a frontend that filters by `parent.empty()`
    // gets a usable target-device list. `list_block_devices` filters disks
    // out for legacy callers, so we pull them via `list_disks`.
    if (auto disks = gucc::disk::list_disks()) {
        out.reserve(disks->size());
        for (auto& d : *disks) {
            out.push_back(from_disk_info(std::move(d)));
        }
    }

    if (auto devs = gucc::disk::list_block_devices()) {
        out.reserve(out.size() + devs->size());
        for (auto& d : *devs) {
            out.push_back(to_device_entry(std::move(d)));
        }
    }
    return out;
}

auto btrfs_subvolumes_inventory(const std::vector<DeviceEntry>& devices) noexcept
    -> std::vector<ExistingBtrfsSubvolume> {
    std::vector<ExistingBtrfsSubvolume> out;
    for (const auto& dev : devices) {
        if (dev.fstype != "btrfs"sv) {
            continue;
        }
        for (const auto& mp : dev.mountpoints) {
            for (auto& subvol : gucc::fs::list_btrfs_subvolumes(mp, ""sv)) {
                out.push_back(ExistingBtrfsSubvolume{
                    .device    = dev.name,
                    .subvolume = std::move(subvol.subvolume),
                });
            }
        }
    }
    return out;
}

auto zfs_pools_inventory() noexcept -> std::vector<ExistingZfsPool> {
    std::vector<ExistingZfsPool> out;
    for (auto& pool : gucc::fs::list_zfs_pools()) {
        out.push_back(ExistingZfsPool{
            .name = std::move(pool.name),
        });
    }
    return out;
}

auto lvm_groups_inventory() noexcept -> std::vector<ExistingLvmGroup> {
    std::vector<ExistingLvmGroup> out;
    for (auto& [name, size] : gucc::lvm::show_volume_groups()) {
        out.push_back(ExistingLvmGroup{
            .name = std::move(name),
            .size = std::move(size),
        });
    }
    return out;
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
    auto src = cachyos::installer::default_btrfs_subvolumes();
    std::vector<BtrfsSubvolumeChoice> out;
    out.reserve(src.size());
    for (auto& subvol : src) {
        out.push_back(BtrfsSubvolumeChoice{
            .subvolume  = std::move(subvol.subvolume),
            .mountpoint = std::move(subvol.mountpoint),
        });
    }
    return out;
}

auto suggest_mountpoint_for_subvolume(std::string_view subvol_path) noexcept -> std::string {
    const auto name = subvol_path.starts_with('/') ? subvol_path.substr(1) : subvol_path;
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
    const auto stem = name.starts_with('@') ? name.substr(1) : name;
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

    // discovery only, never modify the source FS.
    const auto mount_cmd = fmt::format(FMT_COMPILE("mount -t btrfs -o ro '{}' '{}' 2>/dev/null"),
        device, scratch.string());
    if (!gucc::utils::exec_checked(mount_cmd)) {
        fs::remove_all(scratch, ec);
        return std::unexpected(fmt::format("failed to mount {} for inspection", device));
    }

    auto subvols = gucc::fs::list_btrfs_subvolumes(scratch.string(), ""sv);

    // a left-over mount is recoverable
    const auto umount_cmd = fmt::format(FMT_COMPILE("umount '{}' 2>/dev/null"), scratch.string());
    if (!gucc::utils::exec_checked(umount_cmd)) {
        spdlog::warn("inspect_existing_btrfs: failed to unmount scratch at {}", scratch.string());
    }
    fs::remove_all(scratch, ec);

    std::vector<std::string> paths;
    paths.reserve(subvols.size());
    for (auto& subvol : subvols) {
        paths.push_back(std::move(subvol.subvolume));
    }
    return paths;
}

namespace {

// lsblk reports START in 512-byte sectors regardless of physical sector size.
constexpr std::uint64_t kSectorBytes = 512;
// Conservative reserve at each end of a disk when deriving free space without
// parted: ~1 MiB covers MBR/GPT alignment and the GPT backup header. parted's
// `-a optimal` snaps to real alignment at apply time, so this only needs to be
// safe, not exact.
constexpr std::uint64_t kEdgeReserveBytes = 1024ULL * 1024ULL;
// Gaps smaller than this are alignment slack, not usable free space.
constexpr std::uint64_t kMinFreeBytes = 1024ULL * 1024ULL;

// Trailing decimal run of a partition path ("/dev/nvme0n1p2" -> 2, "/dev/sda3" -> 3).
auto trailing_number(std::string_view path) -> std::uint32_t {
    std::size_t i = path.size();
    while (i > 0 && (std::isdigit(static_cast<unsigned char>(path[i - 1])) != 0)) {
        --i;
    }
    std::uint32_t n = 0;
    for (; i < path.size(); ++i) {
        n = (n * 10) + static_cast<std::uint32_t>(path[i] - '0');
    }
    return n;
}

// Resolve a partition's device path: nvme0n1/mmcblk0 use a 'p' separator while
// sd*/vd* append the number directly.
auto partition_path(std::string_view device, std::uint32_t number) -> std::string {
    const bool needs_p = !device.empty() && (std::isdigit(static_cast<unsigned char>(device.back())) != 0);
    return needs_p ? fmt::format("{}p{}", device, number) : fmt::format("{}{}", device, number);
}

// Friendly flag string derived from lsblk's partition type (parted-style names
// the Manual editor recognizes). The ESP is the case that matters for booting.
auto derive_partition_flags(const gucc::disk::BlockDevice& d) -> std::string {
    if (d.parttypename.value_or("") == "EFI System") {
        return "boot, esp";
    }
    return {};
}

// Build the partition list for a disk from the non-root lsblk inventory.
auto collect_partitions(std::string_view device) -> std::vector<PartitionEntry> {
    std::vector<PartitionEntry> out;
    auto devs = gucc::disk::list_block_devices();
    if (!devs) {
        return out;
    }
    for (auto& d : *devs) {
        if (d.type != "part"sv || d.pkname.value_or("") != device) {
            continue;
        }
        const auto start_bytes = d.start.value_or(0) * kSectorBytes;
        const auto size_bytes  = d.size.value_or(0);
        out.push_back(PartitionEntry{
            .number      = trailing_number(d.name),
            .start_bytes = start_bytes,
            .end_bytes   = size_bytes > 0 ? start_bytes + size_bytes - 1 : start_bytes,
            .size_bytes  = size_bytes,
            .device      = d.name,
            .fstype      = std::move(d.fstype),
            .name        = {},
            .flags       = derive_partition_flags(d),
            .part_type   = d.parttypename.value_or(""),
            .label       = d.label.value_or(""),
            .uuid        = std::move(d.uuid),
            .used_bytes  = d.fsused.value_or(0),
        });
    }
    std::ranges::sort(out, {}, &PartitionEntry::start_bytes);
    return out;
}

// Total size of a whole disk, in bytes (non-root, via lsblk).
auto disk_total_bytes(std::string_view device) -> std::uint64_t {
    if (auto disks = gucc::disk::list_disks()) {
        for (const auto& d : *disks) {
            if (d.device == device) {
                return d.size;
            }
        }
    }
    return 0;
}

}  // namespace

auto list_free_space(std::string_view device) noexcept -> std::vector<FreeSpaceRegion> {
    // Derived from lsblk geometry so the view works without root. parted's
    // precise `print free` still backs actuation (it runs as root at install).
    std::vector<FreeSpaceRegion> out;
    const auto total = disk_total_bytes(device);
    if (total <= 2 * kEdgeReserveBytes) {
        return out;
    }
    const auto parts    = collect_partitions(device);
    const auto disk_end = total - kEdgeReserveBytes;  // last usable byte (exclusive of GPT backup)

    auto emit_gap = [&out](std::uint64_t start, std::uint64_t end_exclusive) {
        if (end_exclusive > start && (end_exclusive - start) >= kMinFreeBytes) {
            out.push_back(FreeSpaceRegion{
                .start_bytes = start,
                .end_bytes   = end_exclusive - 1,
                .size_bytes  = end_exclusive - start,
            });
        }
    };

    std::uint64_t cursor = kEdgeReserveBytes;  // first usable byte
    for (const auto& p : parts) {
        if (p.size_bytes == 0) {
            continue;
        }
        emit_gap(cursor, p.start_bytes);
        cursor = std::max(cursor, p.end_bytes + 1);
    }
    emit_gap(cursor, disk_end);
    return out;
}

auto prepare_alongside_partition(std::string_view device,
    std::uint64_t start_bytes, std::uint64_t end_bytes) noexcept
    -> std::expected<std::string, std::string> {
    auto res = gucc::disk::create_partition_in_region(device, start_bytes, end_bytes);
    if (!res) {
        return std::unexpected(std::move(res).error().context);
    }
    return std::move(res).value();
}

auto list_partitions(std::string_view device) noexcept -> std::vector<PartitionEntry> {
    return collect_partitions(device);
}

auto shrinkable_bounds(std::string_view device, std::uint32_t number,
    std::string_view fstype, std::uint64_t current_size_bytes) noexcept
    -> std::expected<std::pair<std::uint64_t, std::uint64_t>, std::string> {
    // Prefer lsblk's FSUSED (non-root) so the shrink slider has a floor even
    // when parted/df would need root; fall back to a filesystem probe otherwise.
    for (const auto& p : collect_partitions(device)) {
        if (p.number == number && p.used_bytes > 0) {
            return std::pair<std::uint64_t, std::uint64_t>{p.used_bytes, current_size_bytes};
        }
    }
    const auto part = partition_path(device, number);
    auto used = gucc::disk::filesystem_used_bytes(part, fstype);
    if (!used) {
        return std::unexpected(std::move(used).error().context);
    }
    return std::pair<std::uint64_t, std::uint64_t>{*used, current_size_bytes};
}

auto apply_partition_operations(std::string_view device, const std::vector<PartitionOp>& ops) noexcept
    -> std::expected<std::string, std::string> {
    std::string last_created;

    for (const auto& op : ops) {
        switch (op.kind) {
        case PartitionOp::Kind::NewTable: {
            if (auto r = gucc::disk::create_partition_table(device, op.table_type); !r) {
                return std::unexpected(std::move(r).error().context);
            }
            break;
        }
        case PartitionOp::Kind::Create: {
            auto created = gucc::disk::create_partition_in_region(device, op.start_bytes, op.end_bytes);
            if (!created) {
                return std::unexpected(std::move(created).error().context);
            }
            last_created = *created;
            if (!op.fstype.empty()) {
                if (auto r = gucc::disk::format_partition(last_created, op.fstype, op.label); !r) {
                    return std::unexpected(std::move(r).error().context);
                }
            }
            if (!op.flags.empty()) {
                const auto num = trailing_number(last_created);
                std::stringstream flags_in{op.flags};
                std::string flag;
                while (std::getline(flags_in, flag, ',')) {
                    while (!flag.empty() && flag.front() == ' ') {
                        flag.erase(flag.begin());
                    }
                    if (flag.empty()) {
                        continue;
                    }
                    if (auto r = gucc::disk::set_partition_flag(device, num, flag, true); !r) {
                        return std::unexpected(std::move(r).error().context);
                    }
                }
            }
            break;
        }
        case PartitionOp::Kind::Delete: {
            if (auto r = gucc::disk::delete_partition(device, op.number); !r) {
                return std::unexpected(std::move(r).error().context);
            }
            break;
        }
        case PartitionOp::Kind::Resize: {
            const auto part = partition_path(device, op.number);
            if (auto r = gucc::disk::shrink_partition(device, op.number, part, op.fstype, op.size_bytes, op.end_bytes); !r) {
                return std::unexpected(std::move(r).error().context);
            }
            break;
        }
        case PartitionOp::Kind::Format: {
            const auto part = partition_path(device, op.number);
            if (auto r = gucc::disk::format_partition(part, op.fstype, op.label); !r) {
                return std::unexpected(std::move(r).error().context);
            }
            break;
        }
        case PartitionOp::Kind::SetFlag: {
            if (auto r = gucc::disk::set_partition_flag(device, op.number, op.flags, op.flag_state); !r) {
                return std::unexpected(std::move(r).error().context);
            }
            break;
        }
        case PartitionOp::Kind::SetLabel: {
            const auto part = partition_path(device, op.number);
            if (auto r = gucc::disk::set_filesystem_label(part, op.fstype, op.label); !r) {
                return std::unexpected(std::move(r).error().context);
            }
            break;
        }
        }
    }
    return last_created;
}

auto apply_partition_plan(std::string_view device, const std::vector<PartitionOp>& ops,
    const std::vector<StagedMount>& mounts) noexcept
    -> std::expected<MountSelections, std::string> {
    // 1. Run the destructive ops; the last partition created is the install target.
    std::string created_device;
    if (!ops.empty()) {
        auto applied = apply_partition_operations(device, ops);
        if (!applied) {
            return std::unexpected(std::move(applied).error());
        }
        created_device = std::move(*applied);
    }

    // 2. Lower the mounts into a PartitionPlan, resolving `created` rows to the
    //    partition the ops just produced.
    PartitionPlan plan{};
    bool have_root = false;
    for (const auto& m : mounts) {
        std::string path = m.created ? created_device : m.device;
        if (path.empty()) {
            return std::unexpected(std::string{"could not resolve the device for mountpoint "} + m.mountpoint);
        }
        // Encrypt before format/mount: luksFormat + open the raw partition, then
        // treat /dev/mapper/<name> as the device the filesystem lands on.
        if (m.luks) {
            if (auto enc = encrypt_partition(LuksFormatRequest{
                    .device      = path,
                    .mapper_name = m.luks_mapper_name,
                    .passphrase  = m.luks_passphrase,
                    .extra_flags = {},
                    .version     = m.luks_version,
                }); !enc) {
                return std::unexpected(std::move(enc).error());
            }
            path = "/dev/mapper/" + m.luks_mapper_name;
        }
        if (m.mountpoint == "/") {
            plan.root = RootPartitionSelection{
                .device           = path,
                .fstype           = m.fstype,
                .mkfs_command     = {},
                .mount_opts       = m.mount_opts,
                .format_requested = m.format_requested,
            };
            have_root = true;
        } else if (m.is_esp) {
            plan.esp = EspSelection{
                .device           = path,
                .mountpoint       = m.mountpoint.empty() ? std::string{"/boot/efi"} : m.mountpoint,
                .format_requested = m.format_requested,
            };
        } else {
            plan.additional.push_back(AdditionalPartSelection{
                .device           = path,
                .mountpoint       = m.mountpoint,
                .fstype           = m.fstype,
                .mkfs_command     = {},
                .mount_opts       = m.mount_opts,
                .format_requested = m.format_requested,
            });
        }
    }
    if (!have_root) {
        return std::unexpected(std::string{"the partition plan has no \"/\" mount"});
    }
    if (plan.root.fstype == "btrfs"sv) {
        plan.btrfs_subvolumes = default_btrfs_layout();
    }

    // 3. Finalize into the MountSelections the orchestrator consumes.
    return finalize_plan(std::move(plan));
}

auto default_zfs_layout(std::string_view zpool_name) noexcept
    -> std::vector<ZfsDatasetChoice> {
    auto src = cachyos::installer::default_zfs_datasets(zpool_name);
    std::vector<ZfsDatasetChoice> out;
    out.reserve(src.size());
    for (auto& dataset : src) {
        out.push_back(ZfsDatasetChoice{
            .dataset    = std::move(dataset.zpath),
            .mountpoint = std::move(dataset.mountpoint),
        });
    }
    return out;
}

auto prepare_default_zpool(std::string_view partition,
    std::string_view zpool_name,
    std::string_view mountpoint) noexcept
    -> std::expected<void, std::string> {
    auto res = cachyos::installer::zfs_auto_pres(partition, zpool_name, mountpoint);
    if (!res) {
        return std::unexpected(std::move(res).error());
    }
    return {};
}

auto encrypt_partition(const LuksFormatRequest& req) noexcept
    -> std::expected<void, std::string> {
    if (auto v = validate_luks_inputs(req.device, req.mapper_name, req.passphrase); !v) {
        return v;
    }
    const auto format_ok = req.version == LuksVersion::Luks2
        ? gucc::crypto::luks2_format(req.passphrase, req.device, req.extra_flags)
        : gucc::crypto::luks1_format(req.passphrase, req.device, req.extra_flags);
    if (!format_ok) {
        return std::unexpected(fmt::format("failed to format LUKS partition {}: {}", req.device, format_ok.error().context));
    }
    const auto open_ok = req.version == LuksVersion::Luks2
        ? gucc::crypto::luks2_open(req.passphrase, req.device, req.mapper_name)
        : gucc::crypto::luks1_open(req.passphrase, req.device, req.mapper_name);
    if (!open_ok) {
        return std::unexpected(fmt::format("failed to open LUKS partition {} as {}: {}",
            req.device, req.mapper_name, open_ok.error().context));
    }
    return {};
}

auto open_encrypted_partition(const LuksOpenRequest& req) noexcept
    -> std::expected<void, std::string> {
    if (auto v = validate_luks_inputs(req.device, req.mapper_name, req.passphrase); !v) {
        return v;
    }
    const auto res = req.version == LuksVersion::Luks2
        ? gucc::crypto::luks2_open(req.passphrase, req.device, req.mapper_name)
        : gucc::crypto::luks1_open(req.passphrase, req.device, req.mapper_name);
    if (!res) {
        return std::unexpected(fmt::format("failed to open LUKS partition {} as {}: {}",
            req.device, req.mapper_name, res.error().context));
    }
    return {};
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

    out.btrfs_subvolumes.reserve(plan.btrfs_subvolumes.size());
    for (auto& choice : plan.btrfs_subvolumes) {
        out.btrfs_subvolumes.push_back(gucc::fs::BtrfsSubvolume{
            .subvolume  = std::move(choice.subvolume),
            .mountpoint = std::move(choice.mountpoint),
        });
    }

    return out;
}

}  // namespace cachyos::installer::partition_planner
