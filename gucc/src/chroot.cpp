#include "detail/chroot.hpp"
#include "gucc/string_utils.hpp"

#include <fcntl.h>      // for AT_FDCWD
#include <sys/mount.h>  // for mount, umount2, MS_*
#include <sys/stat.h>   // for statx

#include <cerrno>   // for errno, EBUSY
#include <cstring>  // for strerror

#include <algorithm>    // for any_of
#include <array>        // for array
#include <filesystem>   // for exists, symlink_status, read_symlink
#include <optional>     // for optional
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

struct MountRow {
    const char* source{};
    std::string_view sub;
    const char* fstype{};
    const char* data{};
    unsigned long flags{};  // NOLINT(google-runtime-int): mount(2) type
    bool optional{false};
    bool make_private{false};
};

constexpr auto kApiFlags = MS_NOSUID | MS_NOEXEC | MS_NODEV;

// NOTE: arch-chroot setup
constexpr std::array kChrootSetup{
    MountRow{.source = "proc", .sub = "/proc"sv, .fstype = "proc", .flags = kApiFlags},
    MountRow{.source = "sys", .sub = "/sys"sv, .fstype = "sysfs", .flags = kApiFlags | MS_RDONLY},
    MountRow{.source = "efivarfs", .sub = "/sys/firmware/efi/efivars"sv, .fstype = "efivarfs", .flags = kApiFlags, .optional = true},
    MountRow{.source = "udev", .sub = "/dev"sv, .fstype = "devtmpfs", .data = "mode=0755", .flags = MS_NOSUID},
    MountRow{.source = "devpts", .sub = "/dev/pts"sv, .fstype = "devpts", .data = "mode=0620,gid=5", .flags = MS_NOSUID | MS_NOEXEC},
    MountRow{.source = "shm", .sub = "/dev/shm"sv, .fstype = "tmpfs", .data = "mode=1777", .flags = MS_NOSUID | MS_NODEV},
    MountRow{.source = "/run", .sub = "/run"sv, .flags = MS_BIND, .make_private = true},
    MountRow{.source = "tmp", .sub = "/tmp"sv, .fstype = "tmpfs", .data = "mode=1777", .flags = MS_STRICTATIME | MS_NODEV | MS_NOSUID},
};

auto mount_id(const std::string& path) noexcept -> std::optional<std::uint64_t> {
    struct statx stx{};
    if (::statx(AT_FDCWD, path.c_str(), 0, STATX_MNT_ID, &stx) != 0 || (stx.stx_mask & STATX_MNT_ID) == 0) {
        return std::nullopt;
    }
    return stx.stx_mnt_id;
}

auto is_mountpoint(const std::string& path) -> bool {
    // stx_attributes is filled regardless of the mask
    struct statx stx{};
    return ::statx(AT_FDCWD, path.c_str(), 0, 0, &stx) == 0 && (stx.stx_attributes_mask & STATX_ATTR_MOUNT_ROOT) != 0
        && (stx.stx_attributes & STATX_ATTR_MOUNT_ROOT) != 0;
}

}  // namespace

namespace gucc::utils::detail {

auto resolv_bind_target(std::string_view root) -> std::optional<std::string> {
    std::error_code ec;
    auto dest = target_path(root, "/etc/resolv.conf"sv);
    if (const auto link = fs::read_symlink(dest, ec); !ec) {
        const auto link_str = link.string();
        dest                = target_path(root, link.is_absolute() ? link_str : target_path("/etc"sv, link_str));
    }
    if (!fs::exists(fs::symlink_status(dest, ec))) {
        return std::nullopt;
    }
    return dest;
}

auto plan_chroot_mounts(std::string_view root, const ChrootHost& host, const std::function<bool(const std::string&)>& is_mountpoint)
    -> std::vector<ChrootMount> {
    std::vector<ChrootMount> plan;
    const auto add_mount = [&](ChrootMount mnt) {
        // a fresh parent mount hides whatever was mounted below it
        const auto parent_planned = std::ranges::any_of(plan, [&mnt](const ChrootMount& planned) {
            return mnt.target.starts_with(planned.target) && std::string_view{mnt.target}.substr(planned.target.size()).starts_with('/');
        });
        if (parent_planned || !is_mountpoint(mnt.target)) {
            plan.push_back(std::move(mnt));
        }
    };

    for (const auto& row : kChrootSetup) {
        // a fresh sysfs mirrors the host one
        if (row.optional && !host.efivars) {
            continue;
        }
        add_mount({.source = row.source, .target = target_path(root, row.sub), .fstype = row.fstype, .data = row.data, .flags = row.flags, .optional = row.optional, .make_private = row.make_private});
    }
    // resolve conf bind
    if (host.resolv_target) {
        add_mount({.source = "/etc/resolv.conf", .target = *host.resolv_target, .flags = MS_BIND});
    }
    return plan;
}

auto chroot_prefix(ChrootMode mode, std::string_view root) -> std::vector<std::string> {
    if (mode == ChrootMode::ArchChroot) {
        return {"/usr/bin/arch-chroot"s, std::string{root}};
    }
    return {
        "/usr/bin/env"s,
        "SHELL=/bin/bash"s,
        "SYSTEMD_IN_CHROOT=1"s,
        "/usr/bin/unshare"s,
        "--fork"s,
        "--pid"s,
        "--kill-child"s,
        "/usr/bin/chroot"s,
        "--"s,
        std::string{root},
    };
}

auto ChrootSession::enter(std::string_view root) noexcept -> Result<ChrootSession> {
    static const bool host_efivars = fs::exists("/sys/firmware/efi/efivars"sv);

    std::error_code ec;
    const ChrootHost host{
        .efivars       = host_efivars,
        .resolv_target = fs::exists("/etc/resolv.conf"sv, ec) ? resolv_bind_target(root) : std::nullopt,
    };

    ChrootSession session;
    for (const auto& entry : plan_chroot_mounts(root, host, is_mountpoint)) {
        if (::mount(entry.source, entry.target.c_str(), entry.fstype, entry.flags, entry.data) != 0) {
            if (entry.optional) {
                continue;
            }
            return make_error(ErrorCode::FileIo, FMT_COMPILE("chroot setup: mount {} on {}: {}"), entry.source, entry.target, std::strerror(errno));
        }
        const auto id = mount_id(entry.target);
        if (!id) {
            ::umount2(entry.target.c_str(), MNT_DETACH);
            return make_error(ErrorCode::FileIo, FMT_COMPILE("chroot setup: no mount id for {}"), entry.target);
        }
        session.m_mounts.push_back({.path = entry.target, .mnt_id = *id});

        // /run mounts
        if (entry.make_private && ::mount(nullptr, entry.target.c_str(), nullptr, MS_PRIVATE, nullptr) != 0) {
            return make_error(ErrorCode::FileIo, FMT_COMPILE("chroot setup: make {} private: {}"), entry.target, std::strerror(errno));
        }
    }
    return session;
}

ChrootSession::~ChrootSession() {
    for (const auto& [path, id] : m_mounts | std::ranges::views::reverse) {
        // something got mounted
        if (mount_id(path) != id) {
            spdlog::warn("[chroot] {} is no longer our mount, leaving it", path);
            continue;
        }
        if (::umount2(path.c_str(), 0) == 0) {
            continue;
        }
        if (errno == EBUSY && ::umount2(path.c_str(), MNT_DETACH) == 0) {
            spdlog::warn("[chroot] {} busy, detached lazily", path);
            continue;
        }
        spdlog::warn("[chroot] failed to unmount {}: {}", path, std::strerror(errno));
    }
}

}  // namespace gucc::utils::detail
