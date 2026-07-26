#include "cachyos/disk.hpp"
#include "cachyos/steps.hpp"
#include "cachyos/types.hpp"

// import gucc
#include "gucc/partition.hpp"
#include "gucc/partition_config.hpp"
#include "gucc/partitioning.hpp"

#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for move
#include <variant>      // for holds_alternative
#include <vector>       // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

using cachyos::installer::MountSelections;

// helper type for the visitor
template <class... Ts>
// NOLINTNEXTLINE(fuchsia-multiple-inheritance): the standard overload-set idiom.
struct overloads : Ts... {
    using Ts::operator()...;
};

auto mount_selections_from_schema(const std::vector<gucc::fs::Partition>& partitions,
    const std::vector<gucc::fs::BtrfsSubvolume>& btrfs_subvolumes) noexcept -> MountSelections {
    // derive mkfs command for all available FS, except ZFS which expected to have empty mkfs cmd
    const auto mkfs_for = [](std::string_view fstype) {
        return std::string{gucc::fs::get_mkfs_command(gucc::fs::string_to_filesystem_type(fstype))};
    };

    MountSelections mounts{};
    for (const auto& part : partitions) {
        if (part.mountpoint == "/"sv) {
            mounts.root = {
                .device           = part.device,
                .fstype           = part.fstype,
                .mkfs_command     = mkfs_for(part.fstype),
                .mount_opts       = part.mount_opts,
                .format_requested = true,
            };
        } else if (part.mountpoint.starts_with("/boot"sv)) {
            mounts.esp = {
                .device           = part.device,
                .mountpoint       = part.mountpoint,
                .format_requested = true,
            };
        } else if (!part.mountpoint.empty()) {
            mounts.additional.push_back({
                .device           = part.device,
                .mountpoint       = part.mountpoint,
                .fstype           = part.fstype,
                .mkfs_command     = mkfs_for(part.fstype),
                .mount_opts       = part.mount_opts,
                .format_requested = true,
            });
        }
    }
    mounts.btrfs_subvolumes = btrfs_subvolumes;
    return mounts;
}

auto mount_selections_from_auto(const std::vector<gucc::fs::Partition>& partitions) noexcept -> MountSelections {
    auto mounts = mount_selections_from_schema(partitions, {});
    if (mounts.root.fstype == "btrfs"sv) {
        mounts.btrfs_subvolumes = cachyos::installer::default_btrfs_subvolumes();
    }
    return mounts;
}

}  // namespace

namespace cachyos::installer::steps {

auto needs_umount(const InstallContext& ctx) noexcept -> bool {
    return !std::holds_alternative<partition_strategy::UseExisting>(ctx.strategy);
}

auto partition(InstallContext& ctx) noexcept
    -> std::expected<void, std::string> {
    const auto is_efi = ctx.system_mode == InstallContext::SystemMode::UEFI;

    // format+mount
    const auto record = [&ctx](MountSelections selections) -> std::expected<void, std::string> {
        auto applied = apply_mount_selections(selections, ctx.mountpoint);
        if (!applied) {
            return std::unexpected(std::move(applied).error());
        }
        ctx.partition_schema = std::move(applied->partitions);
        ctx.swap_device      = std::move(applied->swap_device);
        return {};
    };

    return ctx.strategy.visit(overloads{
        [](const partition_strategy::UseExisting&) -> std::expected<void, std::string> {
            spdlog::info("using mounted target as-is");
            return {};
        },
        [&](const partition_strategy::ApplyLayout& layout) -> std::expected<void, std::string> {
            spdlog::info("formatting and mounting an existing layout");
            return record(layout.selections);
        },
        [&](const partition_strategy::CreateLayout& layout) -> std::expected<void, std::string> {
            spdlog::info("creating {} partitions on '{}'", layout.partitions.size(), layout.device);
            if (auto res = gucc::disk::make_clean_partschema(layout.device, layout.partitions, is_efi); !res) {
                return std::unexpected(fmt::format("failed to write partition table on '{}': {}",
                    layout.device, gucc::to_string(res.error())));
            }
            return record(mount_selections_from_schema(layout.partitions, layout.btrfs_subvolumes));
        },
        [&](const partition_strategy::EraseAndAuto& layout) -> std::expected<void, std::string> {
            spdlog::warn("erasing '{}' and applying the default layout", layout.device);
            // TODO(vnepogodin): refactor that later
            const auto& bios_mode = is_efi ? "UEFI"sv : "BIOS"sv;
            auto partitions       = auto_partition(layout.device, bios_mode, ctx.bootloader);
            if (!partitions) {
                return std::unexpected(partitions.error());
            }
            return record(mount_selections_from_auto(*partitions));
        },
    });
}

}  // namespace cachyos::installer::steps
