#include "gucc/partition_config.hpp"

#include <string>       // for string_literals
#include <string_view>  // for string_view_literals

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace gucc::fs {

auto filesystem_type_to_string(FilesystemType fs_type) noexcept -> std::string_view {
    switch (fs_type) {
    case FilesystemType::Btrfs:
        return "btrfs"sv;
    case FilesystemType::Ext4:
        return "ext4"sv;
    case FilesystemType::F2fs:
        return "f2fs"sv;
    case FilesystemType::Xfs:
        return "xfs"sv;
    case FilesystemType::Vfat:
        return "vfat"sv;
    case FilesystemType::LinuxSwap:
        return "swap"sv;
    case FilesystemType::Zfs:
        return "zfs"sv;
    case FilesystemType::Unknown:
    default:
        return "unknown"sv;
    }
}

auto string_to_filesystem_type(std::string_view fs_name) noexcept -> FilesystemType {
    if (fs_name == "btrfs"sv) {
        return FilesystemType::Btrfs;
    } else if (fs_name == "ext4"sv) {
        return FilesystemType::Ext4;
    } else if (fs_name == "f2fs"sv) {
        return FilesystemType::F2fs;
    } else if (fs_name == "xfs"sv) {
        return FilesystemType::Xfs;
    } else if (fs_name == "vfat"sv || fs_name == "fat32"sv || fs_name == "fat16"sv) {
        return FilesystemType::Vfat;
    } else if (fs_name == "swap"sv || fs_name == "linuxswap"sv) {
        return FilesystemType::LinuxSwap;
    } else if (fs_name == "zfs"sv) {
        return FilesystemType::Zfs;
    }
    return FilesystemType::Unknown;
}

auto get_available_mount_opts(FilesystemType fs_type) noexcept -> std::vector<MountOption> {
    switch (fs_type) {
    case FilesystemType::Btrfs:
        // https://btrfs.readthedocs.io/en/latest/Administration.html
        return {
            {"autodefrag"s, "Enable automatic defragmentation"s},
            {"compress=zlib"s, "Use zlib compression"s},
            {"compress=lzo"s, "Use lzo compression (faster)"s},
            {"compress=zstd"s, "Use zstd compression (balanced)"s},
            {"compress=no"s, "Disable compression"s},
            {"compress-force=zlib"s, "Force zlib compression"s},
            {"compress-force=lzo"s, "Force lzo compression"s},
            {"compress-force=zstd"s, "Force zstd compression"s},
            {"discard"s, "Enable discard/TRIM"s},
            {"noacl"s, "Disable POSIX ACLs"s},
            {"noatime"s, "Do not update access times"s},
            {"nodatasum"s, "Disable data checksumming"s},
            {"nospace_cache"s, "Disable space caching"s},
            {"skip_balance"s, "Skip balance on mount"s},
        };
    case FilesystemType::Ext4:
        // https://www.kernel.org/doc/html/v6.18/admin-guide/ext4.html
        return {
            {"data=journal"s, "Journal all data"s},
            {"data=writeback"s, "Writeback data mode"s},
            {"discard"s, "Enable discard/TRIM"s},
            {"noatime"s, "Do not update access times"s},
            {"nobarrier"s, "Disable write barriers"s},
            {"nodelalloc"s, "Disable delayed allocation"s},
        };
    case FilesystemType::F2fs:
        // https://www.kernel.org/doc/html/v6.18/filesystems/f2fs.html
        return {
            {"data_flush"s, "Enable data flushing"s},
            {"disable_roll_forward"s, "Disable roll-forward recovery"s},
            {"disable_ext_identify"s, "Disable extension identification"s},
            {"discard"s, "Enable discard/TRIM"s},
            {"fastboot"s, "Enable fast boot"s},
            {"flush_merge"s, "Merge flush operations"s},
            {"inline_xattr"s, "Enable inline xattrs"s},
            {"inline_data"s, "Enable inline data"s},
            {"inline_dentry"s, "Enable inline dentries"s},
            {"noacl"s, "Disable POSIX ACLs"s},
            {"nobarrier"s, "Disable write barriers"s},
            {"noextent_cache"s, "Disable extent cache"s},
            {"noinline_data"s, "Disable inline data"s},
        };
    case FilesystemType::Xfs:
        // https://www.kernel.org/doc/html/v6.18/admin-guide/xfs.html
        return {
            {"discard"s, "Enable discard/TRIM"s},
            {"filestreams"s, "Enable filestreams allocator"s},
            {"largeio"s, "Enable large I/O"s},
            {"noalign"s, "Disable stripe alignment"s},
            {"norecovery"s, "Mount without recovery"s},
            {"noquota"s, "Disable quotas"s},
            {"wsync"s, "Enable synchronous writes"s},
        };
    case FilesystemType::Vfat:
        // https://www.kernel.org/doc/html/v6.18/filesystems/vfat.html#vfat-mount-options
        return {
            {"umask=0077"s, "Set file permissions mask"s},
            {"dmask=0077"s, "Set directory permissions mask"s},
            {"fmask=0077"s, "Set file permissions mask"s},
        };
    case FilesystemType::LinuxSwap:
    case FilesystemType::Zfs:
    case FilesystemType::Unknown:
    default:
        return {};
    }
}

auto get_default_mount_opts(FilesystemType fs_type, bool is_ssd) noexcept -> std::string {
    switch (fs_type) {
    case FilesystemType::Btrfs:
        if (is_ssd) {
            return "defaults,noatime,compress=zstd:1"s;
        }
        return "defaults,noatime,compress=zstd"s;
    case FilesystemType::Ext4:
        return "defaults,noatime"s;
    case FilesystemType::F2fs:
        return "defaults,compress_algorithm=lz4,compress_chksum,gc_merge,lazytime"s;
    case FilesystemType::Xfs:
        return "defaults,lazytime,noatime,inode64,logbsize=256k,noquota"s;
    case FilesystemType::Vfat:
        return "defaults,umask=0077"s;
    case FilesystemType::Zfs:
        // ZFS manages its own mount options
        return ""s;
    case FilesystemType::Unknown:
    default:
        return "defaults,noatime"s;
    }
}

auto get_mkfs_command(FilesystemType fs_type) noexcept -> std::string_view {
    switch (fs_type) {
    case FilesystemType::Btrfs:
        return "mkfs.btrfs -f"sv;
    case FilesystemType::Ext4:
        return "mkfs.ext4 -q"sv;
    case FilesystemType::F2fs:
        return "mkfs.f2fs -q"sv;
    case FilesystemType::Xfs:
        return "mkfs.xfs -f"sv;
    case FilesystemType::Vfat:
        return "mkfs.vfat -F32"sv;
    case FilesystemType::LinuxSwap:
        return "mkswap"sv;
    case FilesystemType::Zfs:
        // ZFS uses different creation commands
        return ""sv;
    case FilesystemType::Unknown:
    default:
        return ""sv;
    }
}

auto get_sfdisk_type_alias(FilesystemType fs_type) noexcept -> std::string_view {
    // see https://man.archlinux.org/man/sfdisk.8 for supported aliases
    switch (fs_type) {
    case FilesystemType::Vfat:
        // EFI System partition, means EF for MBR and C12A7328-F81F-11D2-BA4B-00A0C93EC93B for GPT
        return "U"sv;
    case FilesystemType::LinuxSwap:
        // swap area; means 82 for MBR and 0657FD6D-A4AB-43C4-84E5-0933C84B4F4F for GPT
        return "S"sv;
    case FilesystemType::Btrfs:
    case FilesystemType::Ext4:
    case FilesystemType::F2fs:
    case FilesystemType::Xfs:
    case FilesystemType::Zfs:
    case FilesystemType::Unknown:
    default:
        // Linux; means 83 for MBR and 0FC63DAF-8483-4772-8E79-3D69D8477DE4 for GPT.
        return "L"sv;
    }
}

auto get_fstab_fs_name(FilesystemType fs_type) noexcept -> std::string_view {
    switch (fs_type) {
    case FilesystemType::Btrfs:
        return "btrfs"sv;
    case FilesystemType::Ext4:
        return "ext4"sv;
    case FilesystemType::F2fs:
        return "f2fs"sv;
    case FilesystemType::Xfs:
        return "xfs"sv;
    case FilesystemType::Vfat:
        return "vfat"sv;
    case FilesystemType::LinuxSwap:
        return "swap"sv;
    case FilesystemType::Zfs:
        return "zfs"sv;
    case FilesystemType::Unknown:
    default:
        return "auto"sv;
    }
}

}  // namespace gucc::fs
