#include "cachyos/disk.hpp"
#include "cachyos/partition_planner.hpp"
#include "cachyos/steps.hpp"
#include "cachyos/types.hpp"
#include "detail/result.hpp"

// import gucc
#include "gucc/fs_utils.hpp"
#include "gucc/mtab.hpp"
#include "gucc/partition.hpp"
#include "gucc/partitioning.hpp"
#include "gucc/ranges_utils.hpp"

#include <filesystem>   // for path, lexically_relative
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for move
#include <variant>      // for holds_alternative
#include <vector>       // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

using cachyos::installer::MountSelections;

// helper type for the visitor
template <class... Ts>
// NOLINTNEXTLINE(fuchsia-multiple-inheritance): the standard overload-set idiom.
struct overloads : Ts... {
    using Ts::operator()...;
};

auto detect_esp_mountpoint(std::string_view root_mountpoint) noexcept -> std::string {
    const auto entries = gucc::mtab::parse_mtab(root_mountpoint);
    if (!entries) {
        return {};
    }
    for (const auto& entry : *entries) {
        // in-target path
        const auto rel = std::filesystem::path{entry.mountpoint}.lexically_relative(root_mountpoint);
        // reject root
        if (rel.empty() || *rel.begin() != "boot") {
            continue;
        }
        if (gucc::fs::utils::get_mountpoint_fs(entry.mountpoint) == "vfat"sv) {
            // absolute path
            return "/" + rel.generic_string();
        }
    }
    return {};
}

}  // namespace

namespace cachyos::installer::steps {

auto needs_umount(const InstallContext& ctx) noexcept -> bool {
    return !std::holds_alternative<partition_strategy::UseExisting>(ctx.strategy);
}

auto partition(InstallContext& ctx) noexcept
    -> std::expected<void, std::string> {
    const auto is_efi = ctx.is_uefi();

    // format+mount
    const auto record = [&ctx, is_efi](MountSelections selections) -> std::expected<void, std::string> {
        if (is_efi) {
            ctx.uefi_mount = selections.esp.device.empty() ? std::string{} : selections.esp.mountpoint;
        }
        return apply_mount_selections(selections, ctx.mountpoint).transform([&ctx](MountApplicationResult applied) {
            ctx.partition_schema = std::move(applied.partitions);
            ctx.swap_device      = std::move(applied.swap_device);
        });
    };

    return ctx.strategy.visit(overloads{
        [&](const partition_strategy::UseExisting&) -> std::expected<void, std::string> {
            spdlog::info("using mounted target as-is");
            // recover me the ESP mountpoint :_)
            if (is_efi) {
                ctx.uefi_mount = detect_esp_mountpoint(ctx.mountpoint);
            }
            return {};
        },
        [&](const partition_strategy::ApplyLayout& layout) -> std::expected<void, std::string> {
            spdlog::info("formatting and mounting an existing layout");
            return record(layout.selections);
        },
        [&](const partition_strategy::CreateLayout& layout) -> std::expected<void, std::string> {
            // refuse before the partition table is gone
            auto selections = mount_selections_from_schema(layout.partitions, layout.btrfs_subvolumes, is_efi);
            if (!selections) {
                return std::unexpected(std::move(selections).error());
            }

            spdlog::info("creating {} partitions on '{}'", layout.partitions.size(), layout.device);
            if (auto res = lift(gucc::disk::make_clean_partschema(layout.device, layout.partitions, is_efi),
                    fmt::format(FMT_COMPILE("failed to write partition table on '{}'"), layout.device));
                !res) {
                return res;
            }

            // the mount detects the mapper, so fstab, crypttab and kernel params see it
            if (layout.luks_passphrase) {
                const partition_planner::LuksFormatRequest luks{
                    .device      = selections->root.device,
                    .mapper_name = std::string{kLuksRootMapper},
                    .passphrase  = *layout.luks_passphrase,
                    .extra_flags = {},
                };
                if (auto res = partition_planner::encrypt_partition(luks); !res) {
                    return res;
                }
                selections->root.device = gucc::disk::mapper_path(kLuksRootMapper);
                ctx.luks_mappers.emplace_back(kLuksRootMapper);
            }

            // zfs shits
            if (layout.zfs_setup) {
                const auto root = gucc::utils::find_if_optional(layout.partitions, &gucc::fs::Partition::is_root);
                if (!root) {
                    return std::unexpected("zfs root requested but no partition is mounted at '/'"s);
                }
                if (auto res = apply_zfs_root(*layout.zfs_setup, root->device, ctx.mountpoint); !res) {
                    return res;
                }
                ctx.zfs_zpool_names.push_back(layout.zfs_setup->zpool_name);
                ctx.zfs_encrypted = layout.zfs_setup->passphrase.has_value();
                selections->root  = {};
            }

            return record(*std::move(selections));
        },
    });
}

}  // namespace cachyos::installer::steps
